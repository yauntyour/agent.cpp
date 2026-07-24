#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <array>
#include <optional>
#include <sodium.h>

namespace agent::crypto {

class SecureString {
public:
    SecureString() = default;
    explicit SecureString(std::string_view data);
    ~SecureString();

    SecureString(const SecureString&) = delete;
    SecureString& operator=(const SecureString&) = delete;
    SecureString(SecureString&& other) noexcept;
    SecureString& operator=(SecureString&& other) noexcept;

    const char* data() const { return reinterpret_cast<const char*>(m_data.data()); }
    std::string_view view() const { return {data(), m_size}; }
    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    void clear();

private:
    void allocate(size_t n);
    std::vector<unsigned char> m_data;
    size_t m_size = 0;
};

// ── Key derivation (Argon2id via libsodium) ───────────────────────
struct DerivedKey {
    std::array<unsigned char, crypto_secretbox_KEYBYTES> key;
    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt;
};

DerivedKey derive_key(std::string_view password, const unsigned char* salt = nullptr);
DerivedKey derive_key_with_salt(std::string_view password, std::span<const unsigned char, crypto_pwhash_SALTBYTES> salt);

// ── Symmetric encryption (XSalsa20-Poly1305 via libsodium) ────────
std::vector<unsigned char> encrypt(std::span<const unsigned char> plaintext, std::span<const unsigned char, crypto_secretbox_KEYBYTES> key);
std::optional<std::vector<unsigned char>> decrypt(std::span<const unsigned char> ciphertext, std::span<const unsigned char, crypto_secretbox_KEYBYTES> key);

std::string encrypt_string(std::string_view plaintext, std::span<const unsigned char, crypto_secretbox_KEYBYTES> key);
std::optional<std::string> decrypt_string(std::string_view ciphertext_b64, std::span<const unsigned char, crypto_secretbox_KEYBYTES> key);

// ── Password hashing ──────────────────────────────────────────────
std::string hash_password(std::string_view password);
bool verify_password(std::string_view password, std::string_view hash);

// ── Utility ───────────────────────────────────────────────────────
std::string to_base64(std::span<const unsigned char> data);
std::vector<unsigned char> from_base64(std::string_view b64);
std::string random_bytes_base64(size_t n);

} // namespace agent::crypto
