#pragma once
/*
 * bot.hpp — C++ 内置 Telegram / WeChat(ClawBot) 机器人
 * ======================================================
 * 替代 workspace/sys/tg_bot.py 与 workspace/sys/wx_bot.py，
 * 由 app.cpp 单 TU 编译，随主进程以后台线程运行。
 *
 * 依赖: libcurl, libsodium(nlohmann json 由 agent.hpp 提供), qrcodegen(本地 vendored)
 */
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <map>
#include <regex>
#include <random>
#include <chrono>
#include <atomic>
#include <ctime>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "qrcodegen.hpp"
#include "agent.hpp"

namespace bot
{
    using json = nlohmann::json;

    // ════════════════════════════════════════════════════════════════
    // 工具函数
    // ════════════════════════════════════════════════════════════════

    inline std::string str_to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    inline std::string url_encode(const std::string &s)
    {
        std::ostringstream oss;
        oss << std::uppercase << std::hex;
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                oss << c;
            else
                oss << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
        return oss.str();
    }

    inline std::string rand_hex(size_t n)
    {
        static std::mt19937 rng(std::random_device{}());
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < n; ++i)
            oss << std::setw(2) << (int)(rng() % 256);
        return oss.str();
    }

    inline std::string md5_hex(const std::string &data);
    inline std::string aes_ecb_decrypt_pkcs7(const std::string &key16, const std::string &ciphertext);

    // ════════════════════════════════════════════════════════════════
    // AES-128-ECB（运行时生成 S-box，无外部依赖）
    // ════════════════════════════════════════════════════════════════
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
            static std::vector<uint8_t> table = []() {
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
            static std::vector<uint8_t> table = []() {
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
                t[0] = s[0];  t[4] = s[4];  t[8] = s[8];  t[12] = s[12];
                t[1] = s[5];  t[5] = s[9];  t[9] = s[13]; t[13] = s[1];
                t[2] = s[10]; t[6] = s[14]; t[10] = s[2]; t[14] = s[6];
                t[3] = s[15]; t[7] = s[3];  t[11] = s[7]; t[15] = s[11];
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
                t[0] = s[0];  t[4] = s[4];  t[8] = s[8];  t[12] = s[12];
                t[1] = s[13]; t[5] = s[1];  t[9] = s[5];  t[13] = s[9];
                t[2] = s[10]; t[6] = s[14]; t[10] = s[2]; t[14] = s[6];
                t[3] = s[7];  t[7] = s[11]; t[11] = s[15]; t[15] = s[3];
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
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
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
            const int s[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22,
                               5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20,
                               4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                               6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
            const int g[64] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                               1,  6,  11, 0,  5,  10, 15, 4,  9,  14, 3,  8,  13, 2,  7,  12,
                               5,  8,  11, 14, 1,  4,  7,  10, 13, 0,  3,  6,  9,  12, 15, 2,
                               0,  7,  14, 5,  12, 3,  10, 1,  8,  15, 6,  13, 4,  11, 2,  9};
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

    // ════════════════════════════════════════════════════════════════
    // HTTP 请求（每线程独立 CURL*，支持响应头捕获 / 代理 / 超时）
    // ════════════════════════════════════════════════════════════════
    struct HttpResp
    {
        long status = 0;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    namespace http_impl
    {
        inline size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
        {
            ((std::string *)userp)->append((char *)contents, size * nmemb);
            return size * nmemb;
        }
        inline size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
        {
            auto *h = (std::map<std::string, std::string> *)userdata;
            size_t n = size * nitems;
            std::string line(buffer, n);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string key = str_to_lower(line.substr(0, colon));
                std::string val = line.substr(colon + 1);
                // 去掉行尾 \r\n 与首尾空白
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
                    val.pop_back();
                size_t lead = val.find_first_not_of(" \t");
                val = (lead == std::string::npos) ? "" : val.substr(lead);
                (*h)[key] = val;
            }
            return n;
        }
    } // namespace http_impl

    inline HttpResp http_request(const std::string &method, const std::string &url,
                                 const std::string &body, long timeout,
                                 const std::map<std::string, std::string> &hdrs,
                                 const std::string &proxy = "")
    {
        HttpResp resp;
        CURL *curl = curl_easy_init();
        if (!curl)
            return resp;
        std::string header_buf;
        std::map<std::string, std::string> captured;
        struct curl_slist *list = nullptr;
        for (auto &kv : hdrs)
            list = curl_slist_append(list, (kv.first + ": " + kv.second).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_impl::write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, http_impl::header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &captured);
        if (!proxy.empty())
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
        }
        std::string m = str_to_lower(method);
        if (m == "post" || m == "put")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, m.c_str());
            if (!body.empty())
            {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            }
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }
        if (list)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        resp.headers = std::move(captured);
        return resp;
    }

    inline HttpResp http_get(const std::string &url, long timeout,
                             const std::map<std::string, std::string> &hdrs = {},
                             const std::string &proxy = "")
    {
        return http_request("GET", url, "", timeout, hdrs, proxy);
    }

    inline HttpResp http_post(const std::string &url, const std::string &body, long timeout,
                              const std::map<std::string, std::string> &hdrs = {},
                              const std::string &proxy = "")
    {
        return http_request("POST", url, body, timeout, hdrs, proxy);
    }

    // ════════════════════════════════════════════════════════════════
    // Markdown 安全分割（微信 2048 / Telegram 4096）
    // ════════════════════════════════════════════════════════════════
    inline bool is_word_char(unsigned char c) { return std::isalnum(c) || c >= 0x80; }

    inline std::vector<std::pair<size_t, size_t>> find_markdown_spans(const std::string &text)
    {
        std::vector<std::pair<size_t, size_t>> spans;
        std::smatch m;
        // std::regex 无 lookbehind，简化 \*[^*\n]+\* 与 _[^_\n]+_
        std::regex re(R"(```[\s\S]*?```|`[^`\n]+`|!?\[.*?\]\(.*?\)|\*\*.*?\*\*|__.*?__|\*[^*\n]+\*|_[^_\n]+_)");
        std::string::const_iterator it = text.begin(), end = text.end();
        while (std::regex_search(it, end, m, re))
        {
            size_t s = m.position() + (it - text.begin());
            spans.push_back({s, s + m.length()});
            it = m[0].second;
        }
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<size_t, size_t>> merged;
        for (auto &sp : spans)
        {
            if (!merged.empty() && sp.first <= merged.back().second)
                merged.back().second = std::max(merged.back().second, sp.second);
            else
                merged.push_back(sp);
        }
        return merged;
    }

    inline bool is_safe_split_pos(const std::string &text, size_t pos, const std::vector<std::pair<size_t, size_t>> &spans)
    {
        if (pos <= 0 || pos >= text.size())
            return false;
        for (auto &sp : spans)
            if (sp.first < pos && pos < sp.second)
                return false;
        if (pos < text.size() && is_word_char((unsigned char)text[pos]) && is_word_char((unsigned char)text[pos - 1]))
            return false;
        return true;
    }

    inline std::vector<std::string> split_long_paragraph_safe(const std::string &para, size_t max_len)
    {
        if (para.size() <= max_len)
            return {para};
        auto spans = find_markdown_spans(para);
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < para.size())
        {
            size_t end = start + max_len;
            if (end >= para.size())
            {
                parts.push_back(para.substr(start));
                break;
            }
            size_t safe_pos = end;
            for (size_t candidate = end; candidate > start; --candidate)
                if (is_safe_split_pos(para, candidate, spans))
                {
                    if (candidate > 0 && std::string("。！？!?.").find(para[candidate - 1]) != std::string::npos)
                    {
                        safe_pos = candidate;
                        break;
                    }
                }
            if (safe_pos == end && !is_safe_split_pos(para, safe_pos, spans))
            {
                safe_pos = 0;
                for (size_t candidate = end; candidate > start; --candidate)
                    if (is_safe_split_pos(para, candidate, spans))
                    {
                        safe_pos = candidate;
                        break;
                    }
                if (safe_pos == 0)
                    safe_pos = start + max_len;
            }
            std::string part = para.substr(start, safe_pos - start);
            // rstrip
            size_t e = part.find_last_not_of(" \t\r\n");
            if (e == std::string::npos)
                part.clear();
            else
                part.resize(e + 1);
            if (!part.empty())
                parts.push_back(part);
            start = safe_pos;
            while (start < para.size() && para[start] == ' ')
                ++start;
        }
        return parts;
    }

    inline std::vector<std::string> split_markdown_text(const std::string &text, size_t max_len)
    {
        if (text.size() <= max_len)
            return {text};
        // 按段落分割
        std::vector<std::string> paragraphs;
        {
            std::string cur;
            size_t i = 0;
            while (i < text.size())
            {
                if (i + 1 < text.size() && text[i] == '\n' && text[i + 1] == '\n')
                {
                    paragraphs.push_back(cur);
                    cur.clear();
                    i += 2;
                }
                else
                {
                    cur += text[i];
                    ++i;
                }
            }
            paragraphs.push_back(cur);
        }
        std::vector<std::string> parts;
        std::string current;
        for (auto &para : paragraphs)
        {
            std::string candidate = current.empty() ? para : (current + "\n\n" + para);
            if (candidate.size() <= max_len)
            {
                current = candidate;
            }
            else
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                    current.clear();
                }
                if (para.size() > max_len)
                {
                    auto sub = split_long_paragraph_safe(para, max_len);
                    parts.insert(parts.end(), sub.begin(), sub.end());
                }
                else
                {
                    current = para;
                }
            }
        }
        if (!current.empty())
            parts.push_back(current);
        // 最终兜底：强制按长度切割
        std::vector<std::string> final_parts;
        for (auto &p : parts)
        {
            if (p.size() > max_len)
                for (size_t i = 0; i < p.size(); i += max_len)
                    final_parts.push_back(p.substr(i, max_len));
            else
                final_parts.push_back(p);
        }
        return final_parts;
    }

    // ════════════════════════════════════════════════════════════════
    // 解析后端回复（与 Python 脚本一致）
    // ════════════════════════════════════════════════════════════════
    inline std::string extract_assistant_reply(const json &messages)
    {
        if (!messages.is_array())
            return "";
        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        {
            std::string role = it->value("role", "");
            if (role == "user" || role == "tool")
                continue;
            const json &content = it->value("content", json());
            if (content.is_string())
                return content.get<std::string>();
            if (content.is_array())
            {
                std::string combined;
                for (auto &part : content)
                    if (part.value("type", "") == "text")
                        combined += part.value("text", "");
                if (!combined.empty())
                    return combined;
            }
        }
        return "";
    }

    inline std::vector<std::string> extract_images(const json &messages)
    {
        std::vector<std::string> images;
        if (!messages.is_array())
            return images;
        for (auto &msg : messages)
        {
            const json &content = msg.value("content", json());
            if (!content.is_array())
                continue;
            for (auto &part : content)
            {
                if (part.value("type", "") == "image_url")
                {
                    std::string url = part.value("image_url", json()).value("url", "");
                    if (!url.empty())
                        images.push_back(url);
                }
            }
        }
        return images;
    }

    // ════════════════════════════════════════════════════════════════
    // QR 二维码（SVG → base64）
    // ════════════════════════════════════════════════════════════════
    inline std::string qr_svg(const std::string &text)
    {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int size = qr.getSize();
        int border = 4;
        int total = size + 2 * border;
        std::ostringstream oss;
        oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << total << " " << total
            << "\" shape-rendering=\"crispEdges\">";
        oss << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>";
        oss << "<path d=\"";
        bool first = true;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (qr.getModule(x, y))
                {
                    if (!first)
                        oss << " ";
                    oss << "M" << (x + border) << "," << (y + border) << "h1v1h-1z";
                    first = false;
                }
        oss << "\" fill=\"black\"/>";
        oss << "</svg>";
        return oss.str();
    }

    // ════════════════════════════════════════════════════════════════
    // 频道运行状态（供 webui 轮询）
    // ════════════════════════════════════════════════════════════════
    struct ChannelStatus
    {
        std::string name;
        bool running = false;
        std::string state = "idle"; // idle / waiting_qr / running / error / no_token
        std::string detail;
        std::string qr_svg_b64;
        std::string qr_url;
        std::string last_error;
    };

    inline std::mutex &status_mutex()
    {
        static std::mutex m;
        return m;
    }
    inline std::map<std::string, ChannelStatus> &status_map()
    {
        static std::map<std::string, ChannelStatus> m;
        return m;
    }

    inline ChannelStatus &channel_status(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        return status_map()[name];
    }

    inline void update_status(const std::string &name, bool running, const std::string &state,
                              const std::string &detail = "", const std::string &err = "")
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        auto &st = status_map()[name];
        st.name = name;
        st.running = running;
        st.state = state;
        st.detail = detail;
        if (!err.empty())
            st.last_error = err;
    }

    inline json channel_status_json()
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        json arr = json::array();
        for (auto &kv : status_map())
        {
            auto &s = kv.second;
            arr.push_back({{"name", s.name},
                           {"running", s.running},
                           {"state", s.state},
                           {"detail", s.detail},
                           {"last_error", s.last_error}});
        }
        return arr;
    }

    inline json channel_qr_json(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        auto it = status_map().find(name);
        if (it == status_map().end())
            return {{"name", name}, {"state", "idle"}, {"running", false}, {"detail", "频道未启动"}};
        auto &s = it->second;
        return {{"name", s.name},
                {"running", s.running},
                {"state", s.state},
                {"detail", s.detail},
                {"qr_svg", s.qr_svg_b64},
                {"qr_url", s.qr_url}};
    }

    // ════════════════════════════════════════════════════════════════
    // Token 读写（workspace/tokens/<name>.enc，进程内加密/解密）
    // ════════════════════════════════════════════════════════════════
    inline std::string token_dir()
    {
        return run_unit::settings.value("workspace", ".") + "/tokens";
    }

    inline std::string load_channel_token(const std::string &name)
    {
        std::string file_path = token_dir() + "/" + name + ".enc";
        if (!std::filesystem::exists(file_path))
            return "";
        std::string encrypted = tool_unit::readFile(file_path);
        if (!encrypted.empty() && encrypted.back() == '\n')
            encrypted.pop_back();
        return crypto_unit::decrypt(encrypted, crypto_context::key());
    }

    inline bool save_channel_token(const std::string &name, const std::string &token)
    {
        try
        {
            std::filesystem::create_directories(token_dir());
            std::string file_path = token_dir() + "/" + name + ".enc";
            if (token.empty())
            {
                if (std::filesystem::exists(file_path))
                    std::filesystem::remove(file_path);
                return true;
            }
            std::string encrypted = crypto_unit::encrypt(token, crypto_context::key());
            if (encrypted.empty())
                return false;
            tool_unit::writeFile(file_path, encrypted);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // ════════════════════════════════════════════════════════════════
    // 后端调用（agent.cpp /api/input）
    // ════════════════════════════════════════════════════════════════
    inline json call_backend(const std::string &backend_url, const json &req_data, long timeout)
    {
        HttpResp resp = http_post(backend_url, req_data.dump(), timeout,
                                  {{"Content-Type", "application/json"}});
        if (resp.status != 200)
            throw std::runtime_error("backend HTTP " + std::to_string(resp.status));
        return json::parse(resp.body);
    }

    // ════════════════════════════════════════════════════════════════
    // Telegram Bot
    // ════════════════════════════════════════════════════════════════
    namespace tg
    {
        inline std::string api_url(const std::string &token, const std::string &method)
        {
            return "https://api.telegram.org/bot" + token + "/" + method;
        }

        inline json api_call(const std::string &token, const std::string &method, const json &body,
                             long timeout, const std::string &proxy)
        {
            HttpResp resp = http_post(api_url(token, method), body.dump(), timeout,
                                      {{"Content-Type", "application/json"}}, proxy);
            if (resp.status != 200)
                throw std::runtime_error("tg " + method + " HTTP " + std::to_string(resp.status));
            json data = json::parse(resp.body);
            if (!data.value("ok", false))
                throw std::runtime_error("tg " + method + " failed: " + data.value("description", ""));
            return data;
        }

        inline void send_message(const std::string &token, long chat_id, const std::string &text,
                                 const std::string &proxy)
        {
            json body = {{"chat_id", chat_id}, {"text", text}};
            api_call(token, "sendMessage", body, 30, proxy);
        }

        inline void send_chat_action(const std::string &token, long chat_id, const std::string &action,
                                     const std::string &proxy)
        {
            json body = {{"chat_id", chat_id}, {"action", action}};
            try
            {
                api_call(token, "sendChatAction", body, 15, proxy);
            }
            catch (...)
            {
            }
        }

        inline std::string download_file(const std::string &token, const std::string &file_id,
                                         const std::string &proxy)
        {
            json data = api_call(token, "getFile", {{"file_id", file_id}}, 30, proxy);
            std::string path = data.value("result", json()).value("file_path", "");
            if (path.empty())
                return "";
            std::string url = "https://api.telegram.org/file/bot" + token + "/" + path;
            return http_get(url, 60, {}, proxy).body;
        }

        inline void send_photo(const std::string &token, long chat_id, const std::string &img_bytes,
                               const std::string &proxy)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
                return;
            struct curl_slist *list = nullptr;
            list = curl_slist_append(list, "Content-Type: multipart/form-data");
            curl_mime *mime = curl_mime_init(curl);
            {
                curl_mimepart *part = curl_mime_addpart(mime);
                curl_mime_name(part, "chat_id");
                curl_mime_data(part, std::to_string(chat_id).c_str(), CURL_ZERO_TERMINATED);
            }
            {
                curl_mimepart *part = curl_mime_addpart(mime);
                curl_mime_name(part, "photo");
                curl_mime_data(part, img_bytes.data(), img_bytes.size());
                curl_mime_filename(part, "image.jpg");
                curl_mime_type(part, "image/jpeg");
            }
            std::string body;
            curl_easy_setopt(curl, CURLOPT_URL, api_url(token, "sendPhoto").c_str());
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_impl::write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            if (!proxy.empty())
            {
                curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
            }
            curl_easy_perform(curl);
            curl_mime_free(mime);
            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
        }
    } // namespace tg

    inline void tg_worker(json cfg, const std::string &name)
    {
        std::string token = load_channel_token(name);
        if (token.empty())
        {
            update_status(name, false, "no_token", "Bot token 未配置（请在频道管理器填写 Token）");
            return;
        }
        std::string proxy = cfg.value("proxy", "");
        long timeout = cfg.value("timeout", 600);
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
        std::string backend_url = cfg.value("backend_url", "http://127.0.0.1:8080/api/input");
        std::string channel_name = cfg.value("channel", "Telegram");

        update_status(name, true, "running", "长轮询中");
        std::cout << "[" << name << "] Telegram bot started (proxy=" << (proxy.empty() ? "none" : proxy) << ")" << std::endl;

        long offset = 0;
        while (true)
        {
            try
            {
                json body = {{"offset", offset}, {"timeout", 30}};
                HttpResp resp = http_post(tg::api_url(token, "getUpdates"), body.dump(), 60,
                                          {{"Content-Type", "application/json"}}, proxy);
                if (resp.status != 200)
                {
                    update_status(name, true, "running", "getUpdates HTTP " + std::to_string(resp.status));
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    continue;
                }
                json data = json::parse(resp.body);
                if (!data.value("ok", false))
                {
                    // 可能 token 失效
                    update_status(name, false, "error", "Telegram API: " + data.value("description", "unknown"));
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    continue;
                }
                for (auto &upd : data.value("result", json::array()))
                {
                    long uid = upd.value("update_id", 0L);
                    if (uid)
                        offset = uid + 1;
                    if (!upd.contains("message"))
                        continue;
                    const json &msg = upd["message"];
                    long chat_id = msg.value("chat", json()).value("id", 0L);
                    if (!chat_id)
                        continue;
                    std::string text = msg.value("text", "");
                    std::string caption = msg.value("caption", "");

                    if (text == "/start")
                    {
                        try
                        {
                            tg::send_message(token, chat_id, "Welcome to my bot!", proxy);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] /start failed: " << e.what() << std::endl;
                        }
                        continue;
                    }
                    if (!text.empty() && text[0] == '/')
                        continue; // 其他命令忽略

                    tg::send_chat_action(token, chat_id, "typing", proxy);

                    json req = {{"model", model}, {"think", think}, {"channel", channel_name}};
                    if (!text.empty())
                    {
                        req["messages"] = text;
                    }
                    else if (msg.contains("photo") && msg["photo"].is_array() && !msg["photo"].empty())
                    {
                        std::string file_id = msg["photo"].back().value("file_id", "");
                        std::string img_bin = tg::download_file(token, file_id, proxy);
                        if (img_bin.empty())
                        {
                            std::cerr << "[" << name << "] download photo failed" << std::endl;
                            continue;
                        }
                        req["messages"] = caption.empty() ? "[IMAGE]" : caption;
                        req["images"] = json::array({std::string("data:image/jpeg;base64,") + base64::to_base64(img_bin)});
                    }
                    else
                    {
                        try
                        {
                            tg::send_message(token, chat_id, "Message type is empty or not supported.", proxy);
                        }
                        catch (...)
                        {
                        }
                        continue;
                    }

                    std::cout << "[" << name << "] ← " << std::to_string(chat_id) << ": " << text.substr(0, 80) << std::endl;

                    json data_resp;
                    try
                    {
                        data_resp = call_backend(backend_url, req, timeout);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[" << name << "] backend error: " << e.what() << std::endl;
                        try
                        {
                            tg::send_message(token, chat_id, "连接丢失了嘤嘤嘤~", proxy);
                        }
                        catch (...)
                        {
                        }
                        continue;
                    }

                    json messages = data_resp.value("messages", json::array());
                    std::string reply_text = extract_assistant_reply(messages);
                    if (reply_text.empty())
                        reply_text = data_resp.value("content", "");
                    json thinkings = data_resp.value("thinking", json::array());
                    if (!thinkings.is_array())
                        thinkings = data_resp.value("thinkings", json::array());

                    if (thinkings.is_array())
                    {
                        std::string t;
                        for (auto &tk : thinkings)
                            if (tk.is_string() && !tk.get<std::string>().empty())
                            {
                                if (!t.empty())
                                    t += "\n\n";
                                t += "💭 " + tk.get<std::string>();
                            }
                        if (!t.empty())
                            for (auto &part : split_markdown_text(t, 4096))
                                try
                                {
                                    tg::send_message(token, chat_id, part, proxy);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                }
                                catch (const std::exception &e)
                                {
                                    std::cerr << "[" << name << "] send thinking failed: " << e.what() << std::endl;
                                }
                    }

                    if (!reply_text.empty())
                    {
                        auto parts = split_markdown_text(reply_text, 4096);
                        for (size_t i = 0; i < parts.size(); ++i)
                        {
                            try
                            {
                                tg::send_message(token, chat_id, parts[i], proxy);
                            }
                            catch (const std::exception &e)
                            {
                                std::cerr << "[" << name << "] send text failed: " << e.what() << std::endl;
                            }
                            if (i < parts.size() - 1)
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }

                    for (auto &img_b64 : extract_images(messages))
                    {
                        try
                        {
                            std::string data_uri = img_b64;
                            size_t comma = data_uri.find(',');
                            if (comma != std::string::npos)
                                data_uri = data_uri.substr(comma + 1);
                            std::string img_bytes = base64::from_base64(data_uri);
                            tg::send_photo(token, chat_id, img_bytes, proxy);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] send image failed: " << e.what() << std::endl;
                            try
                            {
                                tg::send_message(token, chat_id, "⚠️ 无法解析或发送后端返回的图像。", proxy);
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                update_status(name, true, "running", std::string("轮询错误: ") + e.what());
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // WeChat ClawBot（iLink）
    // ════════════════════════════════════════════════════════════════
    namespace wx
    {
        inline const std::string CDN_HOST = "novac2c.cdn.weixin.qq.com";

        inline std::map<std::string, std::string> make_headers(const std::string &token)
        {
            // X-WECHAT-UIN: base64(pack("<I", rand32))
            uint32_t r = (uint32_t)std::random_device{}() ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
            std::string raw;
            raw.push_back((char)(r & 0xFF));
            raw.push_back((char)((r >> 8) & 0xFF));
            raw.push_back((char)((r >> 16) & 0xFF));
            raw.push_back((char)((r >> 24) & 0xFF));
            return {{"Content-Type", "application/json"},
                    {"AuthorizationType", "ilink_bot_token"},
                    {"X-WECHAT-UIN", base64::to_base64(raw)},
                    {"Authorization", "Bearer " + token}};
        }

        inline std::string make_client_id()
        {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            return "claw_" + std::to_string(now) + "_" + rand_hex(6);
        }

        inline json ilink_call(const std::string &ilink_base, const std::string &path, const json &body,
                               const std::map<std::string, std::string> &hdrs, long timeout)
        {
            HttpResp resp = http_post(ilink_base + path, body.dump(), timeout, hdrs);
            if (resp.body.empty())
                return {{"ret", -1}, {"msg", "empty response"}};
            try
            {
                return json::parse(resp.body);
            }
            catch (...)
            {
                return {{"ret", -2}, {"msg", "non-json response"}, {"raw", resp.body.substr(0, 1000)}};
            }
        }

        inline json ilink_get(const std::string &ilink_base, const std::string &path,
                              const std::map<std::string, std::string> &hdrs = {}, long timeout = 15)
        {
            HttpResp resp = http_get(ilink_base + path, timeout, hdrs);
            if (resp.body.empty())
                return {{"ret", -1}, {"msg", "empty response"}};
            try
            {
                return json::parse(resp.body);
            }
            catch (...)
            {
                return {{"ret", -2}, {"msg", "non-json response"}};
            }
        }

        // 解析微信消息 item_list → (text, images)
        // images: [("encrypted", encrypt_query, aes_key_b64), ("url", url), ("data", b64)]
        inline std::pair<std::string, std::vector<std::array<std::string, 3>>> parse_items(const json &item_list)
        {
            std::string text;
            std::vector<std::array<std::string, 3>> images;
            for (auto &item : item_list)
            {
                int type = item.value("type", 0);
                switch (type)
                {
                case 1: // TEXT
                {
                    std::string t = item.value("text_item", json()).value("text", "");
                    if (!t.empty())
                        text += t;
                    break;
                }
                case 2: // IMAGE
                {
                    json img_item = item.value("image_item", json());
                    if (img_item.is_null())
                        img_item = item.value("img_item", json());
                    json media = img_item.value("media", json());
                    if (media.is_null())
                        media = img_item.value("full", json());
                    std::string eq = media.value("encrypt_query_param", "");
                    std::string ak = media.value("aes_key", "");
                    if (eq.empty() || ak.empty())
                    {
                        json thumb = img_item.value("thumb_media", json());
                        if (thumb.is_null())
                            thumb = img_item.value("thumb", json());
                        if (eq.empty())
                            eq = thumb.value("encrypt_query_param", "");
                        if (ak.empty())
                            ak = thumb.value("aes_key", "");
                    }
                    if (!eq.empty() && !ak.empty())
                    {
                        images.push_back({"encrypted", eq, ak});
                        break;
                    }
                    std::string url = img_item.value("url", img_item.value("cdn_url", img_item.value("image_url", "")));
                    std::string data = img_item.value("data", img_item.value("content", ""));
                    if (!url.empty())
                        images.push_back({"url", url, ""});
                    else if (!data.empty())
                        images.push_back({"data", data, ""});
                    break;
                }
                case 3: // VOICE
                {
                    json v = item.value("voice_item", json());
                    std::string dur = v.value("duration", v.value("play_length", v.value("len", "")));
                    text += "[📞 Voice Message" + (dur.empty() ? "" : " " + dur + "s") + "]";
                    break;
                }
                case 4: // FILE
                {
                    json f = item.value("file_item", json());
                    std::string fn = f.value("file_name", f.value("filename", f.value("title", "未知文件")));
                    std::string sz = f.value("file_size", f.value("size", f.value("file_len", "")));
                    std::string size_str;
                    try
                    {
                        long long b = std::stoll(sz);
                        if (b >= 1048576)
                            size_str = " (" + std::to_string(b / 1048576.0).substr(0, 4) + " MB)";
                        else if (b >= 1024)
                            size_str = " (" + std::to_string(b / 1024.0).substr(0, 4) + " KB)";
                        else
                            size_str = " (" + std::to_string(b) + " B)";
                    }
                    catch (...)
                    {
                    }
                    text += "[📎 File: " + fn + size_str + "]";
                    break;
                }
                case 5: // VIDEO
                {
                    json v = item.value("video_item", json());
                    std::string dur = v.value("duration", v.value("play_length", ""));
                    text += "[🎬 Video Message" + (dur.empty() ? "" : " " + dur + "s") + "]";
                    break;
                }
                default:
                    text += "[📩 Unknown message type=" + std::to_string(type) + "]";
                    break;
                }
            }
            // strip
            size_t b = text.find_first_not_of(" \t\r\n");
            size_t e = text.find_last_not_of(" \t\r\n");
            if (b == std::string::npos)
                text.clear();
            else
                text = text.substr(b, e - b + 1);
            return {text, images};
        }

        // CDN 下载 + AES-ECB 解密
        inline std::string cdn_download(const std::string &encrypt_query, const std::string &aes_key_b64)
        {
            std::string url = "https://" + CDN_HOST + "/c2c/download?encrypted_query_param=" + url_encode(encrypt_query);
            HttpResp resp = http_get(url, 30, {{"User-Agent", "MicroMessenger/8.0"}});
            if (resp.status != 200 || resp.body.size() < 32)
                return "";
            try
            {
                std::string key = wx_cdn_key_bytes(aes_key_b64);
                std::string plain = aes_ecb_decrypt_pkcs7(key, resp.body);
                return plain;
            }
            catch (...)
            {
                // 未加密的图片直接返回
                if ((resp.body.size() >= 2 && (unsigned char)resp.body[0] == 0xFF && (unsigned char)resp.body[1] == 0xD8) ||
                    (resp.body.size() >= 4 && resp.body.substr(0, 4) == "\x89PNG"))
                    return resp.body;
                return "";
            }
        }

        // CDN 上传：POST 优先，失败回退 PUT，捕获 x-encrypted-param
        inline std::string cdn_upload(const std::string &upload_url, const std::string &data)
        {
            for (auto method : {"POST", "PUT"})
            {
                HttpResp resp = http_request(method, upload_url, data, 70,
                                             {{"Content-Type", "application/octet-stream"}});
                if (resp.status == 200 || resp.status == 201)
                {
                    auto it = resp.headers.find("x-encrypted-param");
                    if (it != resp.headers.end() && !it->second.empty())
                        return it->second;
                    try
                    {
                        json body = json::parse(resp.body);
                        std::string ep = body.value("encrypt_query_param", body.value("encrypted_param", ""));
                        if (!ep.empty())
                            return ep;
                    }
                    catch (...)
                    {
                    }
                    // 最后兜底：URL query
                    size_t q = upload_url.find('?');
                    if (q != std::string::npos && q + 1 < upload_url.size())
                        return upload_url.substr(q + 1);
                    return "";
                }
            }
            return "";
        }

        inline json send_text(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                              const std::string &to_user, const std::string &ctx_token, const std::string &text,
                              long timeout = 30)
        {
            json payload = {
                {"msg", {{"to_user_id", to_user},
                         {"message_type", 2},
                         {"message_state", 2},
                         {"context_token", ctx_token},
                         {"client_id", make_client_id()},
                         {"item_list", json::array({{"type", 1}, {"text_item", {{"text", text}}}})}}}};
            return ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, timeout);
        }

        inline json send_image(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                               const std::string &to_user, const std::string &ctx_token,
                               const std::string &img_bytes, const std::string &caption = "")
        {
            try
            {
                std::mt19937 rng(std::random_device{}());
                auto rand_key16 = [&rng]() {
                    std::string k;
                    k.reserve(16);
                    for (int i = 0; i < 16; ++i)
                        k += (char)(rng() % 256);
                    return k;
                };
                auto key_hex_b64 = [](const std::string &k) {
                    std::string hex;
                    hex.reserve(32);
                    static const char *hx = "0123456789abcdef";
                    for (unsigned char c : k)
                    {
                        hex += hx[c >> 4];
                        hex += hx[c & 0x0F];
                    }
                    return base64::to_base64(hex);
                };

                std::string key16 = rand_key16();
                std::string aes_key_b64 = key_hex_b64(key16);

                std::string ciphertext = aes_ecb_encrypt_pkcs7(key16, img_bytes);

                // 缩略图：无图像库，取前 2KB（与 Python 无 PIL 回退一致）
                std::string thumb = img_bytes.size() > 2048 ? img_bytes.substr(0, 2048) : img_bytes;
                std::string thumb_key16 = rand_key16();
                std::string thumb_aes_key_b64 = key_hex_b64(thumb_key16);
                std::string thumb_ciphertext = aes_ecb_encrypt_pkcs7(thumb_key16, thumb);

                // getuploadurl
                std::string filekey = "img_" + rand_hex(4);
                json up_req = {{"to_user_id", to_user},
                               {"filekey", filekey},
                               {"media_type", 1},
                               {"rawsize", (long)img_bytes.size()},
                               {"rawfilemd5", md5_hex(img_bytes)},
                               {"filesize", (long)ciphertext.size()},
                               {"thumb_rawsize", (long)thumb.size()},
                               {"thumb_rawfilemd5", md5_hex(thumb)},
                               {"thumb_filesize", (long)thumb_ciphertext.size()}};
                json up_data;
                try
                {
                    up_data = ilink_call(ilink_base, "/ilink/bot/getuploadurl", up_req, hdrs, 15);
                }
                catch (...)
                {
                    return {{"ret", -1}};
                }
                std::string upload_full_url = up_data.value("upload_full_url", up_data.value("upload_url", ""));
                std::string thumb_upload_url = up_data.value("thumb_upload_full_url", up_data.value("thumb_upload_url", ""));
                if (upload_full_url.empty())
                    return {{"ret", -1}};

                std::string encrypt_query_param = cdn_upload(upload_full_url, ciphertext);
                if (encrypt_query_param.empty())
                    return {{"ret", -1}};
                std::string thumb_encrypt_param = encrypt_query_param;
                if (!thumb_upload_url.empty() && thumb_upload_url != upload_full_url)
                {
                    std::string p = cdn_upload(thumb_upload_url, thumb_ciphertext);
                    if (!p.empty())
                        thumb_encrypt_param = p;
                }

                json image_item = {
                    {"type", 2},
                    {"image_item",
                     {{"media", {{"encrypt_query_param", encrypt_query_param}, {"aes_key", aes_key_b64}}},
                      {"thumb_media", {{"encrypt_query_param", thumb_encrypt_param}, {"aes_key", thumb_aes_key_b64}}}}}};
                json item_list = json::array({image_item});
                if (!caption.empty())
                    item_list.push_back({{"type", 1}, {"text_item", {{"text", caption}}}});

                json payload = {
                    {"msg", {{"to_user_id", to_user},
                             {"message_type", 2},
                             {"message_state", 2},
                             {"context_token", ctx_token},
                             {"client_id", make_client_id()},
                             {"item_list", item_list}}}};
                return ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, 30);
            }
            catch (...)
            {
                return {{"ret", -1}};
            }
        }

        inline void send_paragraphs(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                                    const std::string &to_user, const std::string &ctx_token,
                                    const std::string &text, size_t max_len = 2048, int delay_ms = 200)
        {
            auto parts = split_markdown_text(text, max_len);
            for (size_t i = 0; i < parts.size(); ++i)
            {
                try
                {
                    send_text(ilink_base, hdrs, to_user, ctx_token, parts[i]);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[wx] send_text failed: " << e.what() << std::endl;
                }
                if (i < parts.size() - 1)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }

        // 扫码登录：生成二维码 SVG → 存状态 → 轮询确认 → 返回 token
        inline std::string login_flow(const std::string &ilink_base, const std::string &channel_name)
        {
            json data = ilink_get(ilink_base, "/ilink/bot/get_bot_qrcode?bot_type=3");
            if (data.value("ret", -1) != 0)
            {
                update_status(channel_name, false, "error", "获取二维码失败: " + data.dump());
                return "";
            }
            std::string qrcode_token = data.value("qrcode", "");
            std::string qr_url = data.value("qrcode_img_content", "");
            if (qrcode_token.empty() || qr_url.empty())
            {
                update_status(channel_name, false, "error", "二维码响应缺少 qrcode 字段");
                return "";
            }
            std::cout << "[" << channel_name << "] 请扫码登录: " << qr_url << std::endl;

            // 生成 SVG 并 base64，存入状态供 webui 展示
            {
                std::lock_guard<std::mutex> lk(status_mutex());
                auto &st = status_map()[channel_name];
                st.name = channel_name;
                st.running = true;
                st.state = "waiting_qr";
                st.detail = "等待扫码...";
                st.qr_url = qr_url;
                try
                {
                    st.qr_svg_b64 = base64::to_base64(qr_svg(qr_url));
                }
                catch (...)
                {
                    st.qr_svg_b64.clear();
                }
            }

            std::string last_state;
            for (int i = 0; i < 60; ++i)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                json status;
                try
                {
                    status = ilink_get(ilink_base, "/ilink/bot/get_qrcode_status?qrcode=" + url_encode(qrcode_token));
                }
                catch (...)
                {
                    continue;
                }
                std::string state = status.value("status", "");
                if (!state.empty() && state != last_state)
                {
                    std::cout << "[" << channel_name << "] QR status → " << state << std::endl;
                    last_state = state;
                }
                if (state == "scanned")
                {
                    update_status(channel_name, true, "waiting_qr", "已扫码，等待确认...");
                }
                else if (state == "confirmed" || state == "success")
                {
                    std::string token = status.value("bot_token", status.value("token", ""));
                    if (!token.empty())
                    {
                        update_status(channel_name, true, "running", "扫码成功！");
                        return token;
                    }
                }
                else if (state == "expired")
                {
                    update_status(channel_name, false, "error", "二维码已过期");
                    return "";
                }
            }
            update_status(channel_name, false, "error", "等待扫码超时（120 秒）");
            return "";
        }
    } // namespace wx

    inline void wx_worker(json cfg, const std::string &name)
    {
        std::string ilink_base = cfg.value("ilink_base", "https://ilinkai.weixin.qq.com");
        long timeout = cfg.value("timeout", 600);
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
        std::string channel_name = cfg.value("channel", "WeChat");
        std::string backend_url = cfg.value("backend_url", "http://127.0.0.1:8080/api/input");

        std::string token = load_channel_token(name);
        if (token.empty())
        {
            std::cout << "[" << name << "] BOT_TOKEN 为空，触发扫码登录..." << std::endl;
            update_status(name, true, "waiting_qr", "获取登录二维码...");
            token = wx::login_flow(ilink_base, name);
            if (token.empty())
                return;
            if (!save_channel_token(name, token))
                std::cerr << "[" << name << "] 保存 token 失败" << std::endl;
        }

        update_status(name, true, "running", "轮询中");
        std::cout << "[" << name << "] WeChat ClawBot started, token: " << token.substr(0, 20) << "..." << std::endl;

        auto hdrs = wx::make_headers(token);
        std::string cursor;
        while (true)
        {
            try
            {
                json updates;
                try
                {
                    updates = wx::ilink_call(ilink_base, "/ilink/bot/getupdates",
                                             {{"get_updates_buf", cursor}}, hdrs, 60);
                }
                catch (const std::exception &e)
                {
                    update_status(name, true, "running", std::string("getupdates 失败: ") + e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }

                std::string new_cursor = updates.value("get_updates_buf", "");
                if (!new_cursor.empty() && new_cursor != cursor)
                    cursor = new_cursor;

                for (auto &raw : updates.value("msgs", json::array()))
                {
                    json msg = raw.contains("msg") ? raw["msg"] : raw;
                    std::string to_user = msg.value("from_user_id", raw.value("from_user_id", ""));
                    std::string ctx_token = msg.value("context_token", raw.value("context_token", ""));
                    json item_list = msg.value("item_list", raw.value("item_list", json::array()));

                    auto [user_text, wx_images] = wx::parse_items(item_list);
                    if (to_user.empty() || (user_text.empty() && wx_images.empty()))
                        continue;

                    std::cout << "[" << name << "] → " << to_user.substr(0, 20) << ": "
                              << user_text.substr(0, 80)
                              << (wx_images.empty() ? "" : " +" + std::to_string(wx_images.size()) + " image(s)")
                              << std::endl;

                    // 发送"正在输入"
                    try
                    {
                        wx::ilink_call(ilink_base, "/ilink/bot/sendtyping",
                                       {{"to_user_id", to_user}, {"context_token", ctx_token}}, hdrs, 10);
                    }
                    catch (...)
                    {
                    }

                    json req = {{"model", model}, {"think", think}, {"channel", channel_name}};

                    // 处理图片 → data:image/jpeg;base64
                    json req_images = json::array();
                    for (auto &img_item : wx_images)
                    {
                        std::string kind = img_item[0];
                        std::string img_bytes;
                        if (kind == "encrypted")
                            img_bytes = wx::cdn_download(img_item[1], img_item[2]);
                        else if (kind == "url")
                            img_bytes = http_get(img_item[1], 30).body;
                        else if (kind == "data")
                        {
                            std::string d = img_item[1];
                            for (auto &prefix : {"data:image/jpeg;base64,", "data:image/png;base64,", "data:image/webp;base64,"})
                                if (d.rfind(prefix, 0) == 0)
                                {
                                    d = d.substr(strlen(prefix));
                                    break;
                                }
                            img_bytes = base64::from_base64(d);
                        }
                        if (!img_bytes.empty())
                            req_images.push_back(std::string("data:image/jpeg;base64,") + base64::to_base64(img_bytes));
                    }
                    if (!req_images.empty())
                    {
                        req["messages"] = user_text.empty() ? "[IMAGE]" : user_text;
                        req["images"] = req_images;
                    }
                    else
                    {
                        req["messages"] = user_text;
                    }

                    json data_resp;
                    try
                    {
                        data_resp = call_backend(backend_url, req, timeout);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[" << name << "] backend error: " << e.what() << std::endl;
                        try
                        {
                            wx::send_text(ilink_base, hdrs, to_user, ctx_token, "连接丢失了嘤嘤嘤~");
                        }
                        catch (...)
                        {
                        }
                        continue;
                    }

                    json messages = data_resp.value("messages", json::array());
                    std::string reply_text = extract_assistant_reply(messages);
                    if (reply_text.empty())
                        reply_text = data_resp.value("content", "");
                    json thinkings = data_resp.value("thinking", json::array());
                    if (!thinkings.is_array())
                        thinkings = data_resp.value("thinkings", json::array());

                    if (thinkings.is_array())
                    {
                        std::string t;
                        for (auto &tk : thinkings)
                            if (tk.is_string() && !tk.get<std::string>().empty())
                            {
                                if (!t.empty())
                                    t += "\n\n";
                                t += "💭 " + tk.get<std::string>();
                            }
                        if (!t.empty())
                            wx::send_paragraphs(ilink_base, hdrs, to_user, ctx_token, t);
                    }

                    if (!reply_text.empty())
                        wx::send_paragraphs(ilink_base, hdrs, to_user, ctx_token, reply_text);

                    for (auto &img_b64 : extract_images(messages))
                    {
                        try
                        {
                            std::string data_uri = img_b64;
                            size_t comma = data_uri.find(',');
                            if (comma != std::string::npos)
                                data_uri = data_uri.substr(comma + 1);
                            std::string img_bytes = base64::from_base64(data_uri);
                            wx::send_image(ilink_base, hdrs, to_user, ctx_token, img_bytes);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] send_image failed: " << e.what() << std::endl;
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                update_status(name, true, "running", std::string("循环错误: ") + e.what());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // 启动：扫描 settings.json channels，拉起所有 active 的内置 Bot
    // ════════════════════════════════════════════════════════════════
    // 按名称拉起单个频道的 Bot 线程（已运行则跳过）；找不到/类型不支持返回 false
    inline bool start_channel(const std::string &name)
    {
        {
            std::lock_guard<std::mutex> lk(status_mutex());
            auto it = status_map().find(name);
            if (it != status_map().end() && it->second.running)
                return true;
        }
        if (!run_unit::settings.contains("channels"))
            return false;
        for (auto &ch : run_unit::settings["channels"])
        {
            if (ch.value("name", "") != name)
                continue;
            // 与启动时保持一致：仅 active 频道可拉起
            if (ch.value("status", "") != "active")
                return false;
            json cfg = ch.contains("config") && ch["config"].is_object() ? ch["config"] : json::object();
            std::string path_lower = str_to_lower(ch.value("path", ""));
            if (path_lower.find("tg_bot.py") != std::string::npos)
            {
                std::thread t(tg_worker, cfg, name);
                t.detach();
                std::cout << "[bot] spawned Telegram worker: " << name << std::endl;
                return true;
            }
            else if (path_lower.find("wx_bot.py") != std::string::npos)
            {
                std::thread t(wx_worker, cfg, name);
                t.detach();
                std::cout << "[bot] spawned WeChat worker: " << name << std::endl;
                return true;
            }
            return false;
        }
        return false;
    }

    inline void start_channels()
    {
        static std::once_flag flag;
        std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

        if (!run_unit::settings.contains("channels"))
            return;
        for (auto &ch : run_unit::settings["channels"])
        {
            std::string name = ch.value("name", "");
            std::string status = ch.value("status", "");
            if (name.empty() || status != "active")
                continue;
            start_channel(name);
        }
    }
} // namespace bot

#include "qrcodegen.cpp"
