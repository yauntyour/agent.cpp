#pragma once
#include "core/common.hpp"
#include "core/crypto.hpp"

namespace bot
{
    // 微信 CDN 需要的对称加密 / 摘要（实现在本文件内）
    inline std::string md5_hex(const std::string &data);
    inline std::string aes_ecb_encrypt_pkcs7(const std::string &key16, const std::string &data);
    inline std::string aes_ecb_decrypt_pkcs7(const std::string &key16, const std::string &ciphertext);

    namespace aes
    {
        inline uint8_t gf_mul(uint8_t a, uint8_t b)
        {
            uint8_t p = 0;
            for (int i = 0; i < 8; ++i)
            {
                if (b & 1)
                    p ^= a;
                uint8_t hi = a & 0x80;
                a <<= 1;
                if (hi)
                    a ^= 0x1B;
                b >>= 1;
            }
            return p;
        }

        // GF(2^8) 幂运算（x^254 = 乘法逆元）
        inline uint8_t gf_pow(uint8_t a, int e)
        {
            uint8_t r = 1;
            while (e > 0)
            {
                if (e & 1)
                    r = gf_mul(r, a);
                a = gf_mul(a, a);
                e >>= 1;
            }
            return r;
        }

        inline uint8_t rotl8(uint8_t x, int n) { return (uint8_t)((x << n) | (x >> (8 - n))); }

        inline uint8_t affine(uint8_t x)
        {
            return (uint8_t)(x ^ rotl8(x, 1) ^ rotl8(x, 2) ^ rotl8(x, 3) ^ rotl8(x, 4) ^ 0x63);
        }

        // S-box：sbox[x] = affine(x^254)，0 映射到 affine(0)
        inline const std::vector<uint8_t> &sbox()
        {
            static std::vector<uint8_t> table = []()
            {
                std::vector<uint8_t> t(256);
                for (int i = 0; i < 256; ++i)
                    t[i] = affine((i == 0) ? 0 : gf_pow((uint8_t)i, 254));
                return t;
            }();
            return table;
        }

        // 逆 S-box：对 S-box 置换求逆
        inline const std::vector<uint8_t> &inv_sbox()
        {
            static std::vector<uint8_t> table = []()
            {
                std::vector<uint8_t> t(256);
                const auto &sb = sbox();
                for (int i = 0; i < 256; ++i)
                    t[sb[i]] = (uint8_t)i;
                return t;
            }();
            return table;
        }

        inline void expand_key(const uint8_t key[16], uint8_t rk[176])
        {
            for (int i = 0; i < 16; ++i)
                rk[i] = key[i];
            uint8_t rcon = 1;
            const auto &sb = sbox();
            for (int i = 4; i < 44; ++i)
            {
                uint32_t t = ((uint32_t)rk[4 * (i - 1)] << 24) | ((uint32_t)rk[4 * (i - 1) + 1] << 16) |
                             ((uint32_t)rk[4 * (i - 1) + 2] << 8) | (uint32_t)rk[4 * (i - 1) + 3];
                if (i % 4 == 0)
                {
                    uint32_t r = (t << 8) | (t >> 24); // RotWord
                    uint8_t b[4] = {(uint8_t)(r >> 24), (uint8_t)(r >> 16), (uint8_t)(r >> 8), (uint8_t)r};
                    for (int j = 0; j < 4; ++j)
                        b[j] = sb[b[j]];
                    r = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
                    r ^= ((uint32_t)rcon << 24);
                    rcon = gf_mul(rcon, 2);
                    t = r;
                }
                uint32_t prev = ((uint32_t)rk[4 * (i - 4)] << 24) | ((uint32_t)rk[4 * (i - 4) + 1] << 16) |
                                ((uint32_t)rk[4 * (i - 4) + 2] << 8) | (uint32_t)rk[4 * (i - 4) + 3];
                uint32_t w = prev ^ t;
                rk[4 * i] = (uint8_t)(w >> 24);
                rk[4 * i + 1] = (uint8_t)(w >> 16);
                rk[4 * i + 2] = (uint8_t)(w >> 8);
                rk[4 * i + 3] = (uint8_t)w;
            }
        }

        inline void encrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t rk[176])
        {
            uint8_t s[16], t[16];
            memcpy(s, in, 16);
            const auto &sb = sbox();
            for (int i = 0; i < 16; ++i)
                s[i] ^= rk[i];
            for (int round = 1; round <= 10; ++round)
            {
                for (int i = 0; i < 16; ++i)
                    s[i] = sb[s[i]]; // SubBytes
                // ShiftRows（列主序，行 r 左移 r）
                t[0] = s[0];
                t[4] = s[4];
                t[8] = s[8];
                t[12] = s[12];
                t[1] = s[5];
                t[5] = s[9];
                t[9] = s[13];
                t[13] = s[1];
                t[2] = s[10];
                t[6] = s[14];
                t[10] = s[2];
                t[14] = s[6];
                t[3] = s[15];
                t[7] = s[3];
                t[11] = s[7];
                t[15] = s[11];
                memcpy(s, t, 16);
                if (round < 10)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        uint8_t a0 = s[4 * c], a1 = s[4 * c + 1], a2 = s[4 * c + 2], a3 = s[4 * c + 3];
                        s[4 * c] = gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3;
                        s[4 * c + 1] = a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3;
                        s[4 * c + 2] = a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3);
                        s[4 * c + 3] = gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2);
                    }
                }
                for (int i = 0; i < 16; ++i)
                    s[i] ^= rk[16 * round + i];
            }
            memcpy(out, s, 16);
        }

        inline void decrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t rk[176])
        {
            uint8_t s[16], t[16];
            memcpy(s, in, 16);
            const auto &isb = inv_sbox();
            for (int i = 0; i < 16; ++i)
                s[i] ^= rk[160 + i];
            for (int round = 9; round >= 0; --round)
            {
                // InvShiftRows（行 r 右移 r）
                t[0] = s[0];
                t[4] = s[4];
                t[8] = s[8];
                t[12] = s[12];
                t[1] = s[13];
                t[5] = s[1];
                t[9] = s[5];
                t[13] = s[9];
                t[2] = s[10];
                t[6] = s[14];
                t[10] = s[2];
                t[14] = s[6];
                t[3] = s[7];
                t[7] = s[11];
                t[11] = s[15];
                t[15] = s[3];
                memcpy(s, t, 16);
                for (int i = 0; i < 16; ++i)
                    s[i] = isb[s[i]]; // InvSubBytes
                for (int i = 0; i < 16; ++i)
                    s[i] ^= rk[16 * round + i]; // AddRoundKey
                if (round > 0)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        uint8_t a0 = s[4 * c], a1 = s[4 * c + 1], a2 = s[4 * c + 2], a3 = s[4 * c + 3];
                        s[4 * c] = gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9);
                        s[4 * c + 1] = gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13);
                        s[4 * c + 2] = gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11);
                        s[4 * c + 3] = gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14);
                    }
                }
            }
            memcpy(out, s, 16);
        }
    } // namespace aes

    namespace md5_impl
    {
        inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

        inline const uint32_t *K()
        {
            static const uint32_t table[64] = {
                0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
            return table;
        }

        inline void process_block(uint32_t (&state)[4], const uint8_t *block)
        {
            uint32_t M[16];
            for (int i = 0; i < 16; ++i)
                M[i] = (uint32_t)block[4 * i] | ((uint32_t)block[4 * i + 1] << 8) |
                       ((uint32_t)block[4 * i + 2] << 16) | ((uint32_t)block[4 * i + 3] << 24);
            uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
            const uint32_t *k = K();
            const int s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                               5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
            const int g[64] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                               1, 6, 11, 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12,
                               5, 8, 11, 14, 1, 4, 7, 10, 13, 0, 3, 6, 9, 12, 15, 2,
                               0, 7, 14, 5, 12, 3, 10, 1, 8, 15, 6, 13, 4, 11, 2, 9};
            for (int i = 0; i < 64; ++i)
            {
                uint32_t f, tmp;
                if (i < 16)
                    f = (b & c) | (~b & d);
                else if (i < 32)
                    f = (d & b) | (~d & c);
                else if (i < 48)
                    f = b ^ c ^ d;
                else
                    f = c ^ (b | ~d);
                tmp = d;
                d = c;
                c = b;
                b = b + rotl32(a + f + k[i] + M[g[i]], s[i]);
                a = tmp;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
        }
    } // namespace md5_impl

    inline std::string aes_ecb_encrypt_pkcs7(const std::string &key16, const std::string &data)
    {
        uint8_t rk[176];
        aes::expand_key((const uint8_t *)key16.data(), rk);
        size_t pad_len = 16 - (data.size() % 16);
        std::string padded = data + std::string(pad_len, (char)pad_len);
        std::string out;
        out.resize(padded.size());
        for (size_t i = 0; i < padded.size(); i += 16)
            aes::encrypt_block((const uint8_t *)padded.data() + i, (uint8_t *)out.data() + i, rk);
        return out;
    }

    inline std::string aes_ecb_decrypt_pkcs7(const std::string &key16, const std::string &ciphertext)
    {
        if (ciphertext.empty() || ciphertext.size() % 16 != 0)
            throw std::runtime_error("aes_ecb_decrypt_pkcs7: invalid ciphertext size");
        uint8_t rk[176];
        aes::expand_key((const uint8_t *)key16.data(), rk);
        std::string plain;
        plain.resize(ciphertext.size());
        for (size_t i = 0; i < ciphertext.size(); i += 16)
            aes::decrypt_block((const uint8_t *)ciphertext.data() + i, (uint8_t *)plain.data() + i, rk);
        // PKCS7 unpad
        size_t pad_len = (unsigned char)plain.back();
        if (pad_len >= 1 && pad_len <= 16)
            plain.resize(plain.size() - pad_len);
        return plain;
    }

    // 微信 CDN 密钥解码链: base64 → 32 ASCII hex → 16 字节
    inline std::string wx_cdn_key_bytes(const std::string &aes_key_b64)
    {
        std::string hex_str = base64::from_base64(aes_key_b64);
        std::string key;
        key.reserve(16);
        for (size_t i = 0; i + 1 < hex_str.size(); i += 2)
        {
            auto nib = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return 0;
            };
            key += (char)((nib(hex_str[i]) << 4) | nib(hex_str[i + 1]));
        }
        if (key.size() != 16)
            throw std::runtime_error("wx_cdn_key_bytes: invalid aes key");
        return key;
    }

    // ════════════════════════════════════════════════════════════════
    // MD5（RFC 1321）
    // ════════════════════════════════════════════════════════════════


    inline std::string md5_hex(const std::string &data)
    {
        uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
        std::vector<uint8_t> msg(data.begin(), data.end());
        uint64_t bit_len = (uint64_t)data.size() * 8;
        msg.push_back(0x80);
        while (msg.size() % 64 != 56)
            msg.push_back(0);
        for (int i = 0; i < 8; ++i)
            msg.push_back((uint8_t)(bit_len >> (8 * i)));
        for (size_t i = 0; i < msg.size(); i += 64)
            md5_impl::process_block(state, msg.data() + i);
        std::ostringstream oss;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)(uint8_t)(state[i] >> (8 * j));
        return oss.str();
    }

} // namespace bot
