// qr.hpp — 极简二维码编码器（字节模式，版本 1..10，输出 SVG）
// =============================================================================
// 为什么自己写：微信 iLink（ClawBot）的登录接口给的是一个**待编码的 URL**
// （qrcode_img_content），不是图片；要把二维码画在设置页里，就得本地编码。
// 不引第三方库、不联网，纯计算，80 行数据 + 一次位运算。
//
// 覆盖范围刚好够用：版本 1..10（最多约 270 字节）、四种纠错等级、八种掩码，
// 字节模式。文本超长返回空 —— 二维码不该出现在那种场景里。
//
// 正确性由 _scratch/qr_selftest.cpp 对照 Python 的 qrcode 库逐位验证
// （同版本 / 同纠错 / 同掩码下矩阵必须完全一致）。
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace app::qr
{
    // 纠错等级：0=L 1=M 2=Q 3=H（顺序与下面的块表一致）
    struct matrix
    {
        int size = 0;
        std::vector<uint8_t> cell; // 1 = 黑
        bool at(int x, int y) const { return cell[(size_t)y * size + x] != 0; }
        void set(int x, int y, bool v) { cell[(size_t)y * size + x] = v ? 1 : 0; }
    };

    namespace detail
    {
        // ---- GF(256) ------------------------------------------------------
        struct gf_tables
        {
            std::array<uint8_t, 512> exp{};
            std::array<uint8_t, 256> log{};
            gf_tables()
            {
                int x = 1;
                for (int i = 0; i < 255; i++)
                {
                    exp[i] = (uint8_t)x;
                    log[x] = (uint8_t)i;
                    x <<= 1;
                    if (x & 0x100)
                        x ^= 0x11d; // x^8 + x^4 + x^3 + x^2 + 1
                }
                for (int i = 255; i < 512; i++)
                    exp[i] = exp[i - 255];
            }
        };
        inline const gf_tables &gf()
        {
            static const gf_tables t;
            return t;
        }
        inline uint8_t gmul(uint8_t a, uint8_t b)
        {
            if (a == 0 || b == 0)
                return 0;
            return gf().exp[(int)gf().log[a] + (int)gf().log[b]];
        }

        // ---- Reed-Solomon（与 Nayuki 的实现同形）--------------------------
        inline std::vector<uint8_t> rs_divisor(int degree)
        {
            std::vector<uint8_t> result((size_t)degree, 0);
            result[(size_t)degree - 1] = 1;
            uint8_t root = 1;
            for (int i = 0; i < degree; i++)
            {
                for (int j = 0; j < degree; j++)
                {
                    result[(size_t)j] = gmul(result[(size_t)j], root);
                    if (j + 1 < degree)
                        result[(size_t)j] ^= result[(size_t)j + 1];
                }
                root = gmul(root, 0x02);
            }
            return result;
        }
        inline std::vector<uint8_t> rs_remainder(const std::vector<uint8_t> &data,
                                                 const std::vector<uint8_t> &divisor)
        {
            std::vector<uint8_t> result(divisor.size(), 0);
            for (uint8_t b : data)
            {
                uint8_t factor = b ^ result[0];
                result.erase(result.begin());
                result.push_back(0);
                for (size_t i = 0; i < result.size(); i++)
                    result[i] ^= gmul(divisor[i], factor);
            }
            return result;
        }

        // ---- 块结构表（版本 1..10 × L/M/Q/H，从参考实现导出）--------------
        struct rs_group
        {
            int count, total, data;
        };
        struct rs_row
        {
            int version, ecc;
            rs_group g[3];
            int n;
        };
        inline const rs_row *rs_lookup(int version, int ecc)
        {
            static const rs_row kRows[] = {
                {1, 0, {{1, 26, 19}}, 1},
                {1, 1, {{1, 26, 16}}, 1},
                {1, 2, {{1, 26, 13}}, 1},
                {1, 3, {{1, 26, 9}}, 1},
                {2, 0, {{1, 44, 34}}, 1},
                {2, 1, {{1, 44, 28}}, 1},
                {2, 2, {{1, 44, 22}}, 1},
                {2, 3, {{1, 44, 16}}, 1},
                {3, 0, {{1, 70, 55}}, 1},
                {3, 1, {{1, 70, 44}}, 1},
                {3, 2, {{2, 35, 17}}, 1},
                {3, 3, {{2, 35, 13}}, 1},
                {4, 0, {{1, 100, 80}}, 1},
                {4, 1, {{2, 50, 32}}, 1},
                {4, 2, {{2, 50, 24}}, 1},
                {4, 3, {{4, 25, 9}}, 1},
                {5, 0, {{1, 134, 108}}, 1},
                {5, 1, {{2, 67, 43}}, 1},
                {5, 2, {{2, 33, 15}, {2, 34, 16}}, 2},
                {5, 3, {{2, 33, 11}, {2, 34, 12}}, 2},
                {6, 0, {{2, 86, 68}}, 1},
                {6, 1, {{4, 43, 27}}, 1},
                {6, 2, {{4, 43, 19}}, 1},
                {6, 3, {{4, 43, 15}}, 1},
                {7, 0, {{2, 98, 78}}, 1},
                {7, 1, {{4, 49, 31}}, 1},
                {7, 2, {{2, 32, 14}, {4, 33, 15}}, 2},
                {7, 3, {{4, 39, 13}, {1, 40, 14}}, 2},
                {8, 0, {{2, 121, 97}}, 1},
                {8, 1, {{2, 60, 38}, {2, 61, 39}}, 2},
                {8, 2, {{4, 40, 18}, {2, 41, 19}}, 2},
                {8, 3, {{4, 40, 14}, {2, 41, 15}}, 2},
                {9, 0, {{2, 146, 116}}, 1},
                {9, 1, {{3, 58, 36}, {2, 59, 37}}, 2},
                {9, 2, {{4, 36, 16}, {4, 37, 17}}, 2},
                {9, 3, {{4, 36, 12}, {4, 37, 13}}, 2},
                {10, 0, {{2, 86, 68}, {2, 87, 69}}, 2},
                {10, 1, {{4, 69, 43}, {1, 70, 44}}, 2},
                {10, 2, {{6, 43, 19}, {2, 44, 20}}, 2},
                {10, 3, {{6, 43, 15}, {2, 44, 16}}, 2},
            };
            for (const auto &r : kRows)
                if (r.version == version && r.ecc == ecc)
                    return &r;
            return nullptr;
        }
        inline int total_data_codewords(const rs_row &r)
        {
            int n = 0;
            for (int i = 0; i < r.n; i++)
                n += r.g[i].count * r.g[i].data;
            return n;
        }
        inline int ecc_per_block(const rs_row &r) { return r.g[0].total - r.g[0].data; }

        // 对齐图形中心（版本 1..10）
        inline const std::vector<int> &alignment(int version)
        {
            static const std::vector<std::vector<int>> k = {
                {}, {}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}, {6, 28, 50}};
            return k[(size_t)version];
        }

        // ---- 位缓冲 -------------------------------------------------------
        struct bits
        {
            std::vector<uint8_t> v;
            size_t n = 0;
            void push(int value, int len)
            {
                for (int i = len - 1; i >= 0; i--)
                {
                    if (n % 8 == 0)
                        v.push_back(0);
                    if ((value >> i) & 1)
                        v.back() |= (uint8_t)(0x80u >> (n % 8));
                    n++;
                }
            }
            void push_bit(bool b) { push(b ? 1 : 0, 1); }
        };

        // 格式信息用的纠错指示位：L=01 M=00 Q=11 H=10
        inline int ecc_format_bits(int ecc)
        {
            static const int k[] = {1, 0, 3, 2};
            return k[ecc];
        }
        // BCH(15,5) + 掩码
        inline int format_bits(int ecc, int mask)
        {
            int data = (ecc_format_bits(ecc) << 3) | mask;
            int rem = data;
            for (int i = 0; i < 10; i++)
                rem = (rem << 1) ^ ((rem >> 9) * 0x537);
            return ((data << 10) | rem) ^ 0x5412;
        }
        inline int version_bits(int version)
        {
            int rem = version;
            for (int i = 0; i < 12; i++)
                rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            return (version << 12) | rem;
        }
        inline bool mask_bit(int mask, int x, int y)
        {
            switch (mask)
            {
            case 0: return (x + y) % 2 == 0;
            case 1: return y % 2 == 0;
            case 2: return x % 3 == 0;
            case 3: return (x + y) % 3 == 0;
            case 4: return (x / 3 + y / 2) % 2 == 0;
            case 5: return (x * y) % 2 + (x * y) % 3 == 0;
            case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
            default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
            }
        }
    } // namespace detail

    // 编码成矩阵。force_version=0 表示自动选最小版本；force_mask<0 表示自动挑掩码。
    // 文本放不下（超过版本 10）返回 false。
    inline bool encode(const std::string &text, int ecc, matrix &out, int force_version = 0, int force_mask = -1)
    {
        using namespace detail;
        if (ecc < 0 || ecc > 3)
            return false;
        const size_t len = text.size();
        int version = 0;
        for (int v = force_version > 0 ? force_version : 1; v <= (force_version > 0 ? force_version : 10); v++)
        {
            const rs_row *r = rs_lookup(v, ecc);
            if (!r)
                continue;
            const int count_bits = v <= 9 ? 8 : 16;
            const long need = 4 + count_bits + (long)len * 8;
            if (len <= 0xFFFF && need <= (long)total_data_codewords(*r) * 8)
            {
                version = v;
                break;
            }
            if (force_version > 0)
                return false;
        }
        if (version == 0)
            return false;
        const rs_row *row = rs_lookup(version, ecc);
        if (!row)
            return false;

        // ---- 1. 位流 ------------------------------------------------------
        bits bb;
        bb.push(0x4, 4); // 字节模式
        bb.push((int)len, version <= 9 ? 8 : 16);
        for (unsigned char c : text)
            bb.push(c, 8);
        const int capacity = total_data_codewords(*row) * 8;
        for (int i = 0; i < 4 && (int)bb.n < capacity; i++)
            bb.push_bit(false); // 终止符
        while (bb.n % 8 != 0)
            bb.push_bit(false);
        for (uint8_t pad = 0xEC; (int)bb.n < capacity; pad ^= 0xEC ^ 0x11)
            bb.push(pad, 8);

        // ---- 2. 分块 + 纠错 + 交织 ----------------------------------------
        std::vector<std::vector<uint8_t>> data_blocks, ecc_blocks;
        {
            size_t pos = 0;
            for (int i = 0; i < row->n; i++)
                for (int k = 0; k < row->g[i].count; k++)
                {
                    std::vector<uint8_t> blk(bb.v.begin() + (long)pos,
                                             bb.v.begin() + (long)pos + row->g[i].data);
                    pos += (size_t)row->g[i].data;
                    ecc_blocks.push_back(rs_remainder(blk, rs_divisor(row->g[i].total - row->g[i].data)));
                    data_blocks.push_back(std::move(blk));
                }
        }
        std::vector<uint8_t> final;
        {
            size_t max_data = 0;
            for (auto &b : data_blocks)
                max_data = std::max(max_data, b.size());
            for (size_t i = 0; i < max_data; i++)
                for (auto &b : data_blocks)
                    if (i < b.size())
                        final.push_back(b[i]);
            for (size_t i = 0; i < ecc_blocks[0].size(); i++)
                for (auto &b : ecc_blocks)
                    final.push_back(b[i]);
        }

        // ---- 3. 画功能图形 ------------------------------------------------
        const int size = 17 + 4 * version;
        out.size = size;
        out.cell.assign((size_t)size * size, 0);
        std::vector<uint8_t> reserved((size_t)size * size, 0); // 1 = 功能模块

        auto put = [&](int x, int y, bool v)
        {
            out.set(x, y, v);
            reserved[(size_t)y * size + x] = 1;
        };
        auto finder = [&](int cx, int cy)
        {
            for (int dy = -4; dy <= 4; dy++)
                for (int dx = -4; dx <= 4; dx++)
                {
                    int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= size || y >= size)
                        continue;
                    int d = std::max(std::abs(dx), std::abs(dy));
                    put(x, y, d != 2 && d != 4); // 3x3 实心 + 边框，分隔带留白
                }
        };
        // 定时图形先画：它横穿整行整列，后画的定位角才能盖住两端
        for (int i = 0; i < size; i++)
        {
            put(6, i, i % 2 == 0);
            put(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(size - 4, 3);
        finder(3, size - 4);
        for (int cy : alignment(version))
            for (int cx : alignment(version))
            {
                if ((cx == 6 && cy == 6) || (cx == 6 && cy == size - 7) || (cx == size - 7 && cy == 6))
                    continue;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        put(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        put(8, size - 8, true); // 固定的黑点
        // 预留格式信息区
        for (int i = 0; i < 9; i++)
        {
            if (i != 6)
            {
                reserved[(size_t)8 * size + i] = 1;
                reserved[(size_t)i * size + 8] = 1;
            }
        }
        reserved[(size_t)8 * size + 8] = 1;
        for (int i = 0; i < 8; i++)
        {
            reserved[(size_t)8 * size + (size - 1 - i)] = 1;
            reserved[(size_t)(size - 1 - i) * size + 8] = 1;
        }
        if (version >= 7) // 版本信息区
            for (int i = 0; i < 6; i++)
                for (int j = 0; j < 3; j++)
                {
                    reserved[(size_t)(size - 11 + j) * size + i] = 1;
                    reserved[(size_t)i * size + (size - 11 + j)] = 1;
                }

        // ---- 4. 填数据（右下角起的之字形）--------------------------------
        std::vector<uint8_t> stream;
        for (uint8_t byte : final)
            for (int i = 7; i >= 0; i--)
                stream.push_back((byte >> i) & 1);
        {
            size_t idx = 0;
            for (int right = size - 1; right >= 1; right -= 2)
            {
                if (right == 6)
                    right = 5; // 跳过定时列
                for (int vert = 0; vert < size; vert++)
                {
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    for (int j = 0; j < 2; j++)
                    {
                        int x = right - j;
                        if (reserved[(size_t)y * size + x])
                            continue;
                        out.set(x, y, idx < stream.size() && stream[idx]);
                        idx++;
                    }
                }
            }
        }

        // ---- 5. 选掩码 ----------------------------------------------------
        auto draw_format = [&](matrix &m, int mask)
        {
            int b = format_bits(ecc, mask);
            for (int i = 0; i <= 5; i++)
                m.set(8, i, (b >> i) & 1);
            m.set(8, 7, (b >> 6) & 1);
            m.set(8, 8, (b >> 7) & 1);
            m.set(7, 8, (b >> 8) & 1);
            for (int i = 9; i < 15; i++)
                m.set(14 - i, 8, (b >> i) & 1);
            for (int i = 0; i < 8; i++)
                m.set(size - 1 - i, 8, (b >> i) & 1);
            for (int i = 8; i < 15; i++)
                m.set(8, size - 15 + i, (b >> i) & 1);
        };
        if (force_mask < 0 || force_mask > 7)
        {
            long best = -1;
            int best_mask = 0;
            matrix bestm;
            for (int mask = 0; mask < 8; mask++)
            {
                matrix t;
                t.size = size;
                t.cell.assign(out.cell.size(), 0);
                for (int y = 0; y < size; y++)
                    for (int x = 0; x < size; x++)
                        t.set(x, y, !reserved[(size_t)y * size + x] && out.at(x, y) != mask_bit(mask, x, y));
                for (int y = 0; y < size; y++) // 功能图形不受掩码影响
                    for (int x = 0; x < size; x++)
                        if (reserved[(size_t)y * size + x])
                            t.set(x, y, out.at(x, y));
                draw_format(t, mask);
                if (version >= 7)
                {
                    int vb = version_bits(version);
                    for (int i = 0; i < 18; i++)
                    {
                        int bit = (vb >> i) & 1;
                        t.set(i / 3, size - 11 + i % 3, bit); // 左下 6×3
                        t.set(size - 11 + i % 3, i / 3, bit); // 右上 3×6
                    }
                }
                // 罚分（规则 1/2/4；规则 3 略，足以选出可扫的掩码）
                long score = 0;
                auto run_score = [&](int n)
                {
                    if (n >= 5)
                        score += 3 + (n - 5);
                };
                for (int y = 0; y < size; y++)
                {
                    int run = 1;
                    for (int x = 1; x < size; x++)
                    {
                        if (t.at(x, y) == t.at(x - 1, y))
                            run++;
                        else
                        {
                            run_score(run);
                            run = 1;
                        }
                    }
                    run_score(run);
                }
                for (int x = 0; x < size; x++)
                {
                    int run = 1;
                    for (int y = 1; y < size; y++)
                    {
                        if (t.at(x, y) == t.at(x, y - 1))
                            run++;
                        else
                        {
                            run_score(run);
                            run = 1;
                        }
                    }
                    run_score(run);
                }
                for (int y = 0; y < size - 1; y++)
                    for (int x = 0; x < size - 1; x++)
                    {
                        bool c = t.at(x, y);
                        if (c == t.at(x + 1, y) && c == t.at(x, y + 1) && c == t.at(x + 1, y + 1))
                            score += 3;
                    }
                int dark = 0;
                for (int y = 0; y < size; y++)
                    for (int x = 0; x < size; x++)
                        dark += t.at(x, y) ? 1 : 0;
                int total = size * size;
                score += (std::abs(dark * 20 - total * 10) / total) * 10;
                if (best < 0 || score < best)
                {
                    best = score;
                    best_mask = mask;
                    bestm = std::move(t);
                }
            }
            out = std::move(bestm);
            (void)best_mask;
        }
        else
        {
            int mask = force_mask;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                    if (!reserved[(size_t)y * size + x])
                        out.set(x, y, out.at(x, y) != mask_bit(mask, x, y));
            draw_format(out, mask);
            if (version >= 7)
            {
                int vb = version_bits(version);
                for (int i = 0; i < 18; i++)
                {
                    int bit = (vb >> i) & 1;
                    out.set(i / 3, size - 11 + i % 3, bit);
                    out.set(size - 11 + i % 3, i / 3, bit);
                }
            }
        }
        return true;
    }

    // SVG（单条 path，体积小；浏览器 <img src="data:image/svg+xml;base64,…"> 直接画）
    inline std::string to_svg(const matrix &m, int scale = 6, int border = 4)
    {
        const int n = m.size + border * 2;
        std::string d;
        d.reserve((size_t)m.cell.size() * 8);
        for (int y = 0; y < m.size; y++)
            for (int x = 0; x < m.size; x++)
                if (m.at(x, y))
                    d += "M" + std::to_string(x + border) + " " + std::to_string(y + border) + "h1v1h-1z";
        return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " + std::to_string(n) + " " + std::to_string(n) +
               "' width='" + std::to_string(n * scale) + "' height='" + std::to_string(n * scale) +
               "' shape-rendering='crispEdges'><rect width='100%' height='100%' fill='#fff'/><path fill='#000' d='" +
               d + "'/></svg>";
    }
} // namespace app::qr
