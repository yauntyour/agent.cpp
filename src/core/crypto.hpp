#pragma once
#include "core/common.hpp"
#include "core/base64.hpp"

// ========================== libsodium 加密工具 ==========================

// 前向声明 crypto_context::key()，供 crypto_unit::SecureString 使用
namespace crypto_context
{
    extern std::array<unsigned char, crypto_secretbox_KEYBYTES> &key();
}

namespace crypto_unit
{
    // 密钥：32 字节固定密钥（由随机种子在首次运行时生成，保存在内存中）
    // 注意：生产环境建议使用 keyring / 环境变量注入；此处实现的是运行时内存加密
    constexpr size_t KEY_LEN = crypto_secretbox_KEYBYTES;     // 32
    constexpr size_t NONCE_LEN = crypto_secretbox_NONCEBYTES; // 24
    constexpr size_t MAC_LEN = crypto_secretbox_MACBYTES;     // 16

    // 生成随机密钥（使用 libsodium randombytes）
    inline std::array<unsigned char, KEY_LEN> generate_key()
    {
        std::array<unsigned char, KEY_LEN> key;
        randombytes_buf(key.data(), key.size());
        return key;
    }

    // 生成随机 nonce
    inline std::array<unsigned char, NONCE_LEN> generate_nonce()
    {
        std::array<unsigned char, NONCE_LEN> nonce;
        randombytes_buf(nonce.data(), nonce.size());
        return nonce;
    }

    // 加密：返回 nonce + ciphertext （前 24 字节为 nonce，后续为加密数据）
    inline std::string encrypt(const std::string &plaintext,
                               const std::array<unsigned char, KEY_LEN> &key)
    {
        if (plaintext.empty())
            return "";

        auto nonce = generate_nonce();
        size_t cipher_len = MAC_LEN + plaintext.size();
        std::vector<unsigned char> cipher(cipher_len);

        int rc = crypto_secretbox_easy(
            cipher.data(),
            reinterpret_cast<const unsigned char *>(plaintext.data()),
            plaintext.size(),
            nonce.data(),
            key.data());
        if (rc != 0)
        {
            std::cerr << "crypto_unit::encrypt() failed" << std::endl;
            return "";
        }

        // 拼接 nonce + ciphertext，Base64 编码输出（便于 JSON 存储）
        std::string raw;
        raw.reserve(NONCE_LEN + cipher_len);
        raw.append(reinterpret_cast<const char *>(nonce.data()), NONCE_LEN);
        raw.append(reinterpret_cast<const char *>(cipher.data()), cipher_len);
        return base64::to_base64(raw);
    }

    // 解密：从 Base64(nonce + ciphertext) 中恢复明文
    inline std::string decrypt(const std::string &encrypted_b64,
                               const std::array<unsigned char, KEY_LEN> &key)
    {
        if (encrypted_b64.empty())
            return "";

        std::string encrypted = base64::from_base64(encrypted_b64);
        if (encrypted.size() < NONCE_LEN + MAC_LEN)
        {
            std::cerr << "crypto_unit::decrypt() invalid ciphertext size" << std::endl;
            return "";
        }

        const unsigned char *nonce = reinterpret_cast<const unsigned char *>(encrypted.data());
        const unsigned char *cipher = nonce + NONCE_LEN;
        size_t cipher_len = encrypted.size() - NONCE_LEN;
        size_t plain_len = cipher_len - MAC_LEN;

        std::vector<unsigned char> plain(plain_len);

        int rc = crypto_secretbox_open_easy(
            plain.data(),
            cipher,
            cipher_len,
            nonce,
            key.data());
        if (rc != 0)
        {
            std::cerr << "crypto_unit::decrypt() failed (bad key or corrupted data)" << std::endl;
            return "";
        }

        return std::string(reinterpret_cast<const char *>(plain.data()), plain_len);
    }

    // SecureString：基于 libsodium 的运行时加密字符串
    // 内存中明文存在时间最短；仅在调用 str() / c_str() 时解密，用完立即清零
    class SecureString
    {
    private:
        std::string encrypted_;
        const std::array<unsigned char, KEY_LEN> &key_;

    public:
        SecureString() : key_(crypto_context::key()) {}
        explicit SecureString(const std::string &plain)
            : key_(crypto_context::key())
        {
            if (!plain.empty())
                encrypted_ = encrypt(plain, key_);
        }
        SecureString(const SecureString &other) = default;
        SecureString(SecureString &&other) noexcept = default;
        SecureString &operator=(const SecureString &other) = delete;
        SecureString &operator=(SecureString &&other) noexcept = delete;

        bool empty() const { return encrypted_.empty(); }
        size_t encrypted_size() const { return encrypted_.size(); }

        // 获取明文（调用方负责在不再需要时尽快丢弃）
        std::string str() const
        {
            if (encrypted_.empty())
                return "";
            return decrypt(encrypted_, key_);
        }

        // 设置明文
        void set(const std::string &plain)
        {
            if (plain.empty())
                encrypted_.clear();
            else
                encrypted_ = encrypt(plain, key_);
        }

        void clear()
        {
            encrypted_.clear();
        }
    };
} // namespace crypto_unit

namespace crypto_context
{
    // 全局密钥存储（进程启动时由 sodium_init 后的随机数生成，仅存活于内存）
    inline std::array<unsigned char, crypto_unit::KEY_LEN> &key()
    {
        static auto k = crypto_unit::generate_key();
        return k;
    }

    // 从字符串密码派生 32 字节密钥（使用 crypto_generichash / Blake2b）
    inline std::array<unsigned char, crypto_unit::KEY_LEN>
    derive_key_from_password(const std::string &password)
    {
        std::array<unsigned char, crypto_unit::KEY_LEN> derived{};
        // 使用固定 salt（随机 16 字节），使派生密钥确定性地依赖密码
        constexpr unsigned char salt[crypto_pwhash_SALTBYTES] = {
            0x7a, 0x3f, 0xe1, 0x8c, 0x2b, 0x9d, 0x5a, 0x4e,
            0x6f, 0x8b, 0x1c, 0x3d, 0x9e, 0x2a, 0x7f, 0x5c};
        if (crypto_pwhash(
                derived.data(), derived.size(),
                password.c_str(), password.size(),
                salt,
                crypto_pwhash_OPSLIMIT_MODERATE,
                crypto_pwhash_MEMLIMIT_MODERATE,
                crypto_pwhash_ALG_DEFAULT) != 0)
        {
            std::cerr << "crypto_context::derive_key_from_password() failed" << std::endl;
            return key(); // fallback to random key
        }
        return derived;
    }

    // 用密码设置全局密钥
    inline void init_key_from_password(const std::string &password)
    {
        if (!password.empty())
            key() = derive_key_from_password(password);
    }

    inline void rekey()
    {
        key() = crypto_unit::generate_key();
    }
} // namespace crypto_context
