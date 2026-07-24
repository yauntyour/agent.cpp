#include "utils/crypto.hpp"
#include <cstring>
#include <random>
#include <sodium.h>

namespace agent::crypto {

// ── SecureString ──────────────────────────────────────────────────
SecureString::SecureString(std::string_view data) {
    if (!data.empty()) {
        allocate(data.size());
        std::memcpy(m_data.data(), data.data(), data.size());
    }
}

SecureString::~SecureString() {
    clear();
}

SecureString::SecureString(SecureString&& other) noexcept
    : m_data(std::move(other.m_data)), m_size(other.m_size) {
    other.m_size = 0;
}

SecureString& SecureString::operator=(SecureString&& other) noexcept {
    if (this != &other) {
        clear();
        m_data = std::move(other.m_data);
        m_size = other.m_size;
        other.m_size = 0;
    }
    return *this;
}

void SecureString::clear() {
    if (!m_data.empty()) {
        sodium_memzero(m_data.data(), m_data.size());
        m_data.clear();
    }
    m_size = 0;
}

void SecureString::allocate(size_t n) {
    m_data.resize(n);
    m_size = n;
}

// ── Key Derivation ────────────────────────────────────────────────
DerivedKey derive_key(std::string_view password, const unsigned char* salt) {
    DerivedKey dk;

    if (salt) {
        std::memcpy(dk.salt.data(), salt, crypto_pwhash_SALTBYTES);
    } else {
        randombytes_buf(dk.salt.data(), crypto_pwhash_SALTBYTES);
    }

    if (crypto_pwhash(
            dk.key.data(), crypto_secretbox_KEYBYTES,
            password.data(), password.size(),
            dk.salt.data(),
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE,
            crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("crypto_pwhash: out of memory");
    }

    return dk;
}

DerivedKey derive_key_with_salt(std::string_view password,
                                std::span<const unsigned char, crypto_pwhash_SALTBYTES> salt) {
    return derive_key(password, salt.data());
}

// ── Symmetric Encryption ──────────────────────────────────────────
std::vector<unsigned char> encrypt(std::span<const unsigned char> plaintext,
                                   std::span<const unsigned char, crypto_secretbox_KEYBYTES> key) {
    std::vector<unsigned char> nonce(crypto_secretbox_NONCEBYTES);
    randombytes_buf(nonce.data(), nonce.size());

    std::vector<unsigned char> ciphertext(nonce.size() + crypto_secretbox_MACBYTES + plaintext.size());
    std::memcpy(ciphertext.data(), nonce.data(), nonce.size());

    crypto_secretbox_easy(
        ciphertext.data() + nonce.size(),
        plaintext.data(), plaintext.size(),
        nonce.data(), key.data());

    return ciphertext;
}

std::optional<std::vector<unsigned char>> decrypt(std::span<const unsigned char> ciphertext,
                                                   std::span<const unsigned char, crypto_secretbox_KEYBYTES> key) {
    if (ciphertext.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
        return std::nullopt;
    }

    auto nonce = ciphertext.subspan(0, crypto_secretbox_NONCEBYTES);
    auto ct = ciphertext.subspan(crypto_secretbox_NONCEBYTES);
    size_t plaintext_len = ct.size() - crypto_secretbox_MACBYTES;

    std::vector<unsigned char> plaintext(plaintext_len);

    if (crypto_secretbox_open_easy(
            plaintext.data(),
            ct.data(), ct.size(),
            nonce.data(), key.data()) != 0) {
        return std::nullopt; // Decryption failed
    }

    return plaintext;
}

std::string encrypt_string(std::string_view plaintext,
                           std::span<const unsigned char, crypto_secretbox_KEYBYTES> key) {
    auto ct = encrypt(
        std::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()),
        key);
    return to_base64(ct);
}

std::optional<std::string> decrypt_string(std::string_view ciphertext_b64,
                                          std::span<const unsigned char, crypto_secretbox_KEYBYTES> key) {
    auto ct = from_base64(ciphertext_b64);
    auto pt = decrypt(ct, key);
    if (!pt) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(pt->data()), pt->size());
}

// ── Password Hashing ──────────────────────────────────────────────
std::string hash_password(std::string_view password) {
    char hash_out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
            hash_out,
            password.data(), password.size(),
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        throw std::runtime_error("crypto_pwhash_str: out of memory");
    }
    return std::string(hash_out);
}

bool verify_password(std::string_view password, std::string_view hash) {
    return crypto_pwhash_str_verify(
               hash.data(),
               password.data(), password.size()) == 0;
}

// ── Base64 ────────────────────────────────────────────────────────
std::string to_base64(std::span<const unsigned char> data) {
    size_t b64_max_len = sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string result(b64_max_len, '\0');
    if (sodium_bin2base64(
            result.data(), result.size(),
            data.data(), data.size(),
            sodium_base64_VARIANT_ORIGINAL) == nullptr) {
        return {};
    }
    // Trim trailing null
    auto pos = result.find('\0');
    if (pos != std::string::npos) result.resize(pos);
    return result;
}

std::vector<unsigned char> from_base64(std::string_view b64) {
    size_t bin_max_len = b64.size() * 3 / 4 + 1;
    std::vector<unsigned char> result(bin_max_len);
    size_t bin_len = 0;
    if (sodium_base642bin(
            result.data(), result.size(),
            b64.data(), b64.size(),
            nullptr, &bin_len, nullptr,
            sodium_base64_VARIANT_ORIGINAL) != 0) {
        return {};
    }
    result.resize(bin_len);
    return result;
}

std::string random_bytes_base64(size_t n) {
    std::vector<unsigned char> buf(n);
    randombytes_buf(buf.data(), n);
    return to_base64(buf);
}

} // namespace agent::crypto
