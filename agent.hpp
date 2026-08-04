#pragma once
#ifndef __AGENT__H__
#define __AGENT__H__
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <list>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <unistd.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <sodium.h>

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#elif __linux__
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#if defined(__cpp_lib_bit_cast)
#include <bit> // For std::bit_cast.
#endif

namespace base64
{

    namespace detail
    {

#if defined(__cpp_lib_bit_cast)
        using std::bit_cast;
#else
        template <class To, class From>
        std::enable_if_t<sizeof(To) == sizeof(From) &&
                             std::is_trivially_copyable_v<From> &&
                             std::is_trivially_copyable_v<To>,
                         To> bit_cast(const From &src) noexcept
        {
            static_assert(std::is_trivially_constructible_v<To>,
                          "This implementation additionally requires "
                          "destination type to be trivially constructible");

            To dst;
            std::memcpy(&dst, &src, sizeof(To));
            return dst;
        }
#endif

        inline constexpr char padding_char{'='};
        inline constexpr uint32_t bad_char{0x01FFFFFF};

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) ||  \
    (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN) ||              \
    (defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN) ||                 \
    (defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN) ||                    \
    (defined(__sun) && defined(__SVR4) && defined(_BIG_ENDIAN)) ||          \
    defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
    defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__) ||         \
    defined(_M_PPC)
#define __BIG_ENDIAN__
#elif (defined(__BYTE_ORDER__) &&                                              \
       __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || /* gcc */                 \
    (defined(__BYTE_ORDER) &&                                                  \
     __BYTE_ORDER == __LITTLE_ENDIAN) /* linux header */                       \
    || (defined(_BYTE_ORDER) && _BYTE_ORDER == _LITTLE_ENDIAN) ||              \
    (defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN) /* mingw header */ || \
    (defined(__sun) && defined(__SVR4) &&                                      \
     defined(_LITTLE_ENDIAN)) || /* solaris */                                 \
    defined(__ARMEL__) ||                                                      \
    defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(_MIPSEL) ||      \
    defined(__MIPSEL) || defined(__MIPSEL__) || defined(_M_IX86) ||            \
    defined(_M_X64) || defined(_M_IA64) || /* msvc for intel processors */     \
    defined(_M_ARM) ||                                                         \
    defined(_M_ARM64) /* msvc code on arm executes in little endian mode */
#define __LITTLE_ENDIAN__
#endif
#endif

#if !defined(__LITTLE_ENDIAN__) & !defined(__BIG_ENDIAN__)
#error "UNKNOWN Platform / endianness. Configure endianness explicitly."
#endif

#if defined(__LITTLE_ENDIAN__)
        std::array<std::uint32_t, 256> constexpr decode_table_0 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x000000f8, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x000000fc,
            0x000000d0, 0x000000d4, 0x000000d8, 0x000000dc, 0x000000e0, 0x000000e4,
            0x000000e8, 0x000000ec, 0x000000f0, 0x000000f4, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00000004, 0x00000008, 0x0000000c, 0x00000010, 0x00000014, 0x00000018,
            0x0000001c, 0x00000020, 0x00000024, 0x00000028, 0x0000002c, 0x00000030,
            0x00000034, 0x00000038, 0x0000003c, 0x00000040, 0x00000044, 0x00000048,
            0x0000004c, 0x00000050, 0x00000054, 0x00000058, 0x0000005c, 0x00000060,
            0x00000064, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00000068, 0x0000006c, 0x00000070, 0x00000074, 0x00000078,
            0x0000007c, 0x00000080, 0x00000084, 0x00000088, 0x0000008c, 0x00000090,
            0x00000094, 0x00000098, 0x0000009c, 0x000000a0, 0x000000a4, 0x000000a8,
            0x000000ac, 0x000000b0, 0x000000b4, 0x000000b8, 0x000000bc, 0x000000c0,
            0x000000c4, 0x000000c8, 0x000000cc, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_1 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0000e003, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x0000f003,
            0x00004003, 0x00005003, 0x00006003, 0x00007003, 0x00008003, 0x00009003,
            0x0000a003, 0x0000b003, 0x0000c003, 0x0000d003, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00001000, 0x00002000, 0x00003000, 0x00004000, 0x00005000, 0x00006000,
            0x00007000, 0x00008000, 0x00009000, 0x0000a000, 0x0000b000, 0x0000c000,
            0x0000d000, 0x0000e000, 0x0000f000, 0x00000001, 0x00001001, 0x00002001,
            0x00003001, 0x00004001, 0x00005001, 0x00006001, 0x00007001, 0x00008001,
            0x00009001, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0000a001, 0x0000b001, 0x0000c001, 0x0000d001, 0x0000e001,
            0x0000f001, 0x00000002, 0x00001002, 0x00002002, 0x00003002, 0x00004002,
            0x00005002, 0x00006002, 0x00007002, 0x00008002, 0x00009002, 0x0000a002,
            0x0000b002, 0x0000c002, 0x0000d002, 0x0000e002, 0x0000f002, 0x00000003,
            0x00001003, 0x00002003, 0x00003003, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_2 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00800f00, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00c00f00,
            0x00000d00, 0x00400d00, 0x00800d00, 0x00c00d00, 0x00000e00, 0x00400e00,
            0x00800e00, 0x00c00e00, 0x00000f00, 0x00400f00, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00400000, 0x00800000, 0x00c00000, 0x00000100, 0x00400100, 0x00800100,
            0x00c00100, 0x00000200, 0x00400200, 0x00800200, 0x00c00200, 0x00000300,
            0x00400300, 0x00800300, 0x00c00300, 0x00000400, 0x00400400, 0x00800400,
            0x00c00400, 0x00000500, 0x00400500, 0x00800500, 0x00c00500, 0x00000600,
            0x00400600, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00800600, 0x00c00600, 0x00000700, 0x00400700, 0x00800700,
            0x00c00700, 0x00000800, 0x00400800, 0x00800800, 0x00c00800, 0x00000900,
            0x00400900, 0x00800900, 0x00c00900, 0x00000a00, 0x00400a00, 0x00800a00,
            0x00c00a00, 0x00000b00, 0x00400b00, 0x00800b00, 0x00c00b00, 0x00000c00,
            0x00400c00, 0x00800c00, 0x00c00c00, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_3 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x003e0000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x003f0000,
            0x00340000, 0x00350000, 0x00360000, 0x00370000, 0x00380000, 0x00390000,
            0x003a0000, 0x003b0000, 0x003c0000, 0x003d0000, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00010000, 0x00020000, 0x00030000, 0x00040000, 0x00050000, 0x00060000,
            0x00070000, 0x00080000, 0x00090000, 0x000a0000, 0x000b0000, 0x000c0000,
            0x000d0000, 0x000e0000, 0x000f0000, 0x00100000, 0x00110000, 0x00120000,
            0x00130000, 0x00140000, 0x00150000, 0x00160000, 0x00170000, 0x00180000,
            0x00190000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x001a0000, 0x001b0000, 0x001c0000, 0x001d0000, 0x001e0000,
            0x001f0000, 0x00200000, 0x00210000, 0x00220000, 0x00230000, 0x00240000,
            0x00250000, 0x00260000, 0x00270000, 0x00280000, 0x00290000, 0x002a0000,
            0x002b0000, 0x002c0000, 0x002d0000, 0x002e0000, 0x002f0000, 0x00300000,
            0x00310000, 0x00320000, 0x00330000, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        // TODO fix decoding tables to avoid the need for different indices in big
        // endian?
        inline constexpr size_t decidx0{0};
        inline constexpr size_t decidx1{1};
        inline constexpr size_t decidx2{2};

#elif defined(__BIG_ENDIAN__)

        std::array<std::uint32_t, 256> constexpr decode_table_0 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00f80000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00fc0000,
            0x00d00000, 0x00d40000, 0x00d80000, 0x00dc0000, 0x00e00000, 0x00e40000,
            0x00e80000, 0x00ec0000, 0x00f00000, 0x00f40000, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00040000, 0x00080000, 0x000c0000, 0x00100000, 0x00140000, 0x00180000,
            0x001c0000, 0x00200000, 0x00240000, 0x00280000, 0x002c0000, 0x00300000,
            0x00340000, 0x00380000, 0x003c0000, 0x00400000, 0x00440000, 0x00480000,
            0x004c0000, 0x00500000, 0x00540000, 0x00580000, 0x005c0000, 0x00600000,
            0x00640000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00680000, 0x006c0000, 0x00700000, 0x00740000, 0x00780000,
            0x007c0000, 0x00800000, 0x00840000, 0x00880000, 0x008c0000, 0x00900000,
            0x00940000, 0x00980000, 0x009c0000, 0x00a00000, 0x00a40000, 0x00a80000,
            0x00ac0000, 0x00b00000, 0x00b40000, 0x00b80000, 0x00bc0000, 0x00c00000,
            0x00c40000, 0x00c80000, 0x00cc0000, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_1 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0003e000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x0003f000,
            0x00034000, 0x00035000, 0x00036000, 0x00037000, 0x00038000, 0x00039000,
            0x0003a000, 0x0003b000, 0x0003c000, 0x0003d000, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00001000, 0x00002000, 0x00003000, 0x00004000, 0x00005000, 0x00006000,
            0x00007000, 0x00008000, 0x00009000, 0x0000a000, 0x0000b000, 0x0000c000,
            0x0000d000, 0x0000e000, 0x0000f000, 0x00010000, 0x00011000, 0x00012000,
            0x00013000, 0x00014000, 0x00015000, 0x00016000, 0x00017000, 0x00018000,
            0x00019000, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0001a000, 0x0001b000, 0x0001c000, 0x0001d000, 0x0001e000,
            0x0001f000, 0x00020000, 0x00021000, 0x00022000, 0x00023000, 0x00024000,
            0x00025000, 0x00026000, 0x00027000, 0x00028000, 0x00029000, 0x0002a000,
            0x0002b000, 0x0002c000, 0x0002d000, 0x0002e000, 0x0002f000, 0x00030000,
            0x00031000, 0x00032000, 0x00033000, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_2 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00000f80, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000fc0,
            0x00000d00, 0x00000d40, 0x00000d80, 0x00000dc0, 0x00000e00, 0x00000e40,
            0x00000e80, 0x00000ec0, 0x00000f00, 0x00000f40, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00000040, 0x00000080, 0x000000c0, 0x00000100, 0x00000140, 0x00000180,
            0x000001c0, 0x00000200, 0x00000240, 0x00000280, 0x000002c0, 0x00000300,
            0x00000340, 0x00000380, 0x000003c0, 0x00000400, 0x00000440, 0x00000480,
            0x000004c0, 0x00000500, 0x00000540, 0x00000580, 0x000005c0, 0x00000600,
            0x00000640, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x00000680, 0x000006c0, 0x00000700, 0x00000740, 0x00000780,
            0x000007c0, 0x00000800, 0x00000840, 0x00000880, 0x000008c0, 0x00000900,
            0x00000940, 0x00000980, 0x000009c0, 0x00000a00, 0x00000a40, 0x00000a80,
            0x00000ac0, 0x00000b00, 0x00000b40, 0x00000b80, 0x00000bc0, 0x00000c00,
            0x00000c40, 0x00000c80, 0x00000cc0, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        std::array<std::uint32_t, 256> constexpr decode_table_3 = {
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0000003e, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x0000003f,
            0x00000034, 0x00000035, 0x00000036, 0x00000037, 0x00000038, 0x00000039,
            0x0000003a, 0x0000003b, 0x0000003c, 0x0000003d, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x00000000,
            0x00000001, 0x00000002, 0x00000003, 0x00000004, 0x00000005, 0x00000006,
            0x00000007, 0x00000008, 0x00000009, 0x0000000a, 0x0000000b, 0x0000000c,
            0x0000000d, 0x0000000e, 0x0000000f, 0x00000010, 0x00000011, 0x00000012,
            0x00000013, 0x00000014, 0x00000015, 0x00000016, 0x00000017, 0x00000018,
            0x00000019, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x0000001a, 0x0000001b, 0x0000001c, 0x0000001d, 0x0000001e,
            0x0000001f, 0x00000020, 0x00000021, 0x00000022, 0x00000023, 0x00000024,
            0x00000025, 0x00000026, 0x00000027, 0x00000028, 0x00000029, 0x0000002a,
            0x0000002b, 0x0000002c, 0x0000002d, 0x0000002e, 0x0000002f, 0x00000030,
            0x00000031, 0x00000032, 0x00000033, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff,
            0x01ffffff, 0x01ffffff, 0x01ffffff, 0x01ffffff};

        // TODO fix decoding tables to avoid the need for different indices in big
        // endian?
        inline constexpr size_t decidx0{1};
        inline constexpr size_t decidx1{2};
        inline constexpr size_t decidx2{3};

#endif

        std::array<char, 256> constexpr encode_table_0 = {
            'A', 'A', 'A', 'A', 'B', 'B', 'B', 'B', 'C', 'C', 'C', 'C', 'D', 'D', 'D',
            'D', 'E', 'E', 'E', 'E', 'F', 'F', 'F', 'F', 'G', 'G', 'G', 'G', 'H', 'H',
            'H', 'H', 'I', 'I', 'I', 'I', 'J', 'J', 'J', 'J', 'K', 'K', 'K', 'K', 'L',
            'L', 'L', 'L', 'M', 'M', 'M', 'M', 'N', 'N', 'N', 'N', 'O', 'O', 'O', 'O',
            'P', 'P', 'P', 'P', 'Q', 'Q', 'Q', 'Q', 'R', 'R', 'R', 'R', 'S', 'S', 'S',
            'S', 'T', 'T', 'T', 'T', 'U', 'U', 'U', 'U', 'V', 'V', 'V', 'V', 'W', 'W',
            'W', 'W', 'X', 'X', 'X', 'X', 'Y', 'Y', 'Y', 'Y', 'Z', 'Z', 'Z', 'Z', 'a',
            'a', 'a', 'a', 'b', 'b', 'b', 'b', 'c', 'c', 'c', 'c', 'd', 'd', 'd', 'd',
            'e', 'e', 'e', 'e', 'f', 'f', 'f', 'f', 'g', 'g', 'g', 'g', 'h', 'h', 'h',
            'h', 'i', 'i', 'i', 'i', 'j', 'j', 'j', 'j', 'k', 'k', 'k', 'k', 'l', 'l',
            'l', 'l', 'm', 'm', 'm', 'm', 'n', 'n', 'n', 'n', 'o', 'o', 'o', 'o', 'p',
            'p', 'p', 'p', 'q', 'q', 'q', 'q', 'r', 'r', 'r', 'r', 's', 's', 's', 's',
            't', 't', 't', 't', 'u', 'u', 'u', 'u', 'v', 'v', 'v', 'v', 'w', 'w', 'w',
            'w', 'x', 'x', 'x', 'x', 'y', 'y', 'y', 'y', 'z', 'z', 'z', 'z', '0', '0',
            '0', '0', '1', '1', '1', '1', '2', '2', '2', '2', '3', '3', '3', '3', '4',
            '4', '4', '4', '5', '5', '5', '5', '6', '6', '6', '6', '7', '7', '7', '7',
            '8', '8', '8', '8', '9', '9', '9', '9', '+', '+', '+', '+', '/', '/', '/',
            '/'};

        std::array<char, 256> constexpr encode_table_1 = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
            'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
            'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
            't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', '+', '/', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
            'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
            'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '+', '/', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
            'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
            'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
            'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/', 'A', 'B', 'C',
            'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
            'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
            'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+',
            '/'};

    } // namespace detail

    template <class OutputBuffer, class InputIterator>
    inline OutputBuffer encode_into(InputIterator begin, InputIterator end)
    {
        typedef std::decay_t<decltype(*begin)> input_value_type;
        static_assert(std::is_same_v<input_value_type, char> ||
                      std::is_same_v<input_value_type, signed char> ||
                      std::is_same_v<input_value_type, unsigned char> ||
                      std::is_same_v<input_value_type, std::byte>);
        typedef typename OutputBuffer::value_type output_value_type;
        static_assert(std::is_same_v<output_value_type, char> ||
                      std::is_same_v<output_value_type, signed char> ||
                      std::is_same_v<output_value_type, unsigned char> ||
                      std::is_same_v<output_value_type, std::byte>);
        const size_t binarytextsize = end - begin;
        const size_t encodedsize = (binarytextsize / 3 + (binarytextsize % 3 > 0))
                                   << 2;
        OutputBuffer encoded(encodedsize, detail::padding_char);

        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&*begin);
        char *currEncoding = reinterpret_cast<char *>(&encoded[0]);

        for (size_t i = binarytextsize / 3; i; --i)
        {
            const uint8_t t1 = *bytes++;
            const uint8_t t2 = *bytes++;
            const uint8_t t3 = *bytes++;
            *currEncoding++ = detail::encode_table_0[t1];
            *currEncoding++ =
                detail::encode_table_1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
            *currEncoding++ =
                detail::encode_table_1[((t2 & 0x0F) << 2) | ((t3 >> 6) & 0x03)];
            *currEncoding++ = detail::encode_table_1[t3];
        }

        switch (binarytextsize % 3)
        {
        case 0:
        {
            break;
        }
        case 1:
        {
            const uint8_t t1 = bytes[0];
            *currEncoding++ = detail::encode_table_0[t1];
            *currEncoding++ = detail::encode_table_1[(t1 & 0x03) << 4];
            break;
        }
        case 2:
        {
            const uint8_t t1 = bytes[0];
            const uint8_t t2 = bytes[1];
            *currEncoding++ = detail::encode_table_0[t1];
            *currEncoding++ =
                detail::encode_table_1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)];
            *currEncoding++ = detail::encode_table_1[(t2 & 0x0F) << 2];
            break;
        }
        default:
        {
            throw std::runtime_error{"Invalid base64 encoded data"};
        }
        }

        return encoded;
    }

    template <class OutputBuffer>
    inline OutputBuffer encode_into(std::string_view data)
    {
        return encode_into<OutputBuffer>(std::begin(data), std::end(data));
    }

    inline std::string to_base64(std::string_view data)
    {
        return encode_into<std::string>(std::begin(data), std::end(data));
    }

    template <class OutputBuffer>
    inline OutputBuffer decode_into(std::string_view base64Text)
    {
        typedef typename OutputBuffer::value_type output_value_type;
        static_assert(std::is_same_v<output_value_type, char> ||
                      std::is_same_v<output_value_type, signed char> ||
                      std::is_same_v<output_value_type, unsigned char> ||
                      std::is_same_v<output_value_type, std::byte>);
        if (base64Text.empty())
        {
            return OutputBuffer();
        }

        if ((base64Text.size() & 3) != 0)
        {
            throw std::runtime_error{
                "Invalid base64 encoded data - Size not divisible by 4"};
        }

        const size_t numPadding =
            std::count(base64Text.rbegin(), base64Text.rbegin() + 4, '=');
        if (numPadding > 2)
        {
            throw std::runtime_error{
                "Invalid base64 encoded data - Found more than 2 padding signs"};
        }

        const size_t decodedsize = (base64Text.size() * 3 >> 2) - numPadding;
        OutputBuffer decoded(decodedsize, '.');

        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&base64Text[0]);
        char *currDecoding = reinterpret_cast<char *>(&decoded[0]);

        for (size_t i = (base64Text.size() >> 2) - (numPadding != 0); i; --i)
        {
            const uint8_t t1 = *bytes++;
            const uint8_t t2 = *bytes++;
            const uint8_t t3 = *bytes++;
            const uint8_t t4 = *bytes++;

            const uint32_t d1 = detail::decode_table_0[t1];
            const uint32_t d2 = detail::decode_table_1[t2];
            const uint32_t d3 = detail::decode_table_2[t3];
            const uint32_t d4 = detail::decode_table_3[t4];

            const uint32_t temp = d1 | d2 | d3 | d4;

            if (temp >= detail::bad_char)
            {
                throw std::runtime_error{
                    "Invalid base64 encoded data - Invalid character"};
            }

            // Use bit_cast instead of union and type punning to avoid
            // undefined behaviour risk:
            // https://en.wikipedia.org/wiki/Type_punning#Use_of_union
            const std::array<char, 4> tempBytes =
                detail::bit_cast<std::array<char, 4>, uint32_t>(temp);

            *currDecoding++ = tempBytes[detail::decidx0];
            *currDecoding++ = tempBytes[detail::decidx1];
            *currDecoding++ = tempBytes[detail::decidx2];
        }

        switch (numPadding)
        {
        case 0:
        {
            break;
        }
        case 1:
        {
            const uint8_t t1 = *bytes++;
            const uint8_t t2 = *bytes++;
            const uint8_t t3 = *bytes++;

            const uint32_t d1 = detail::decode_table_0[t1];
            const uint32_t d2 = detail::decode_table_1[t2];
            const uint32_t d3 = detail::decode_table_2[t3];

            const uint32_t temp = d1 | d2 | d3;

            if (temp >= detail::bad_char)
            {
                throw std::runtime_error{
                    "Invalid base64 encoded data - Invalid character"};
            }

            // Use bit_cast instead of union and type punning to avoid
            // undefined behaviour risk:
            // https://en.wikipedia.org/wiki/Type_punning#Use_of_union
            const std::array<char, 4> tempBytes =
                detail::bit_cast<std::array<char, 4>, uint32_t>(temp);
            *currDecoding++ = tempBytes[detail::decidx0];
            *currDecoding++ = tempBytes[detail::decidx1];
            break;
        }
        case 2:
        {
            const uint8_t t1 = *bytes++;
            const uint8_t t2 = *bytes++;

            const uint32_t d1 = detail::decode_table_0[t1];
            const uint32_t d2 = detail::decode_table_1[t2];

            const uint32_t temp = d1 | d2;

            if (temp >= detail::bad_char)
            {
                throw std::runtime_error{
                    "Invalid base64 encoded data - Invalid character"};
            }

            const std::array<char, 4> tempBytes =
                detail::bit_cast<std::array<char, 4>, uint32_t>(temp);
            *currDecoding++ = tempBytes[detail::decidx0];
            break;
        }
        default:
        {
            throw std::runtime_error{
                "Invalid base64 encoded data - Invalid padding number"};
        }
        }

        return decoded;
    }

    template <class OutputBuffer, class InputIterator>
    inline OutputBuffer decode_into(InputIterator begin, InputIterator end)
    {
        typedef std::decay_t<decltype(*begin)> input_value_type;
        static_assert(std::is_same_v<input_value_type, char> ||
                      std::is_same_v<input_value_type, signed char> ||
                      std::is_same_v<input_value_type, unsigned char> ||
                      std::is_same_v<input_value_type, std::byte>);
        std::string_view data(reinterpret_cast<const char *>(&*begin), end - begin);
        return decode_into<OutputBuffer>(data);
    }

    inline std::string from_base64(std::string_view data)
    {
        return decode_into<std::string>(data);
    }

} // namespace base64

std::string get_system_status()
{
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed;

    // ----- CPU 使用率 -----
    {
        // 静态变量保存上一次 CPU 时间
        static bool cpuInitialized = false;
        static unsigned long long prevTotal = 0, prevIdle = 0;
        unsigned long long totalTime = 0, idleTime = 0;
        bool valid = false;

#ifdef _WIN32
        FILETIME idleFt, kernelFt, userFt;
        if (GetSystemTimes(&idleFt, &kernelFt, &userFt))
        {
            auto to_ull = [](const FILETIME &ft) -> unsigned long long
            {
                return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            };
            auto idle = to_ull(idleFt);
            auto kernel = to_ull(kernelFt);
            auto user = to_ull(userFt);
            // kernel 时间中已包含 idle
            totalTime = kernel + user;
            idleTime = idle;
            valid = true;
        }

#elif __APPLE__
        processor_info_array_t infoArray;
        mach_msg_type_number_t infoCount;
        natural_t cpuCount;
        if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                                &cpuCount, &infoArray, &infoCount) == KERN_SUCCESS)
        {
            long long totalUser = 0, totalSystem = 0, totalIdle = 0, totalNice = 0;
            for (natural_t i = 0; i < cpuCount; ++i)
            {
                auto base = i * CPU_STATE_MAX;
                totalUser += infoArray[base + CPU_STATE_USER];
                totalSystem += infoArray[base + CPU_STATE_SYSTEM];
                totalIdle += infoArray[base + CPU_STATE_IDLE];
                totalNice += infoArray[base + CPU_STATE_NICE];
            }
            vm_deallocate(mach_task_self(), (vm_address_t)infoArray,
                          infoCount * sizeof(*infoArray));
            totalTime = totalUser + totalSystem + totalIdle + totalNice;
            idleTime = totalIdle;
            valid = true;
        }

#elif __linux__
        std::ifstream statFile("/proc/stat");
        if (statFile.is_open())
        {
            std::string line;
            if (std::getline(statFile, line))
            {
                // 期望格式: cpu  user nice system idle iowait irq softirq ...
                char cpu[8];
                unsigned long long user, nice, sys, idle, iowait, irq, softirq;
                int matched = sscanf(line.c_str(), "%7s %llu %llu %llu %llu %llu %llu %llu",
                                     cpu, &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
                if (matched >= 8 && strcmp(cpu, "cpu") == 0)
                {
                    totalTime = user + nice + sys + idle + iowait + irq + softirq;
                    idleTime = idle + iowait; // iowait 也可视为空闲
                    valid = true;
                }
            }
        }
#endif

        if (valid)
        {
            if (cpuInitialized)
            {
                unsigned long long deltaTotal = totalTime - prevTotal;
                unsigned long long deltaIdle = idleTime - prevIdle;
                if (deltaTotal > 0)
                {
                    double usage = (1.0 - (double)deltaIdle / deltaTotal) * 100.0;
                    oss << "CPU Usage: " << usage << "%\n";
                }
                else
                {
                    oss << "CPU Usage: N/A\n";
                }
            }
            else
            {
                oss << "CPU Usage: N/A (first call)\n";
            }
            prevTotal = totalTime;
            prevIdle = idleTime;
            cpuInitialized = true;
        }
        else
        {
            oss << "CPU Usage: unavailable\n";
        }
    }

    // ----- 内存信息 -----
    {
        unsigned long long totalMem = 0, availMem = 0;

#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus))
        {
            totalMem = memStatus.ullTotalPhys;
            availMem = memStatus.ullAvailPhys;
        }

#elif __APPLE__
        // 总内存
        int64_t memsize = 0;
        size_t size = sizeof(memsize);
        sysctlbyname("hw.memsize", &memsize, &size, NULL, 0);
        totalMem = memsize;

        // 可用内存: free + inactive 页
        vm_statistics64_data_t vmStat;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info64_t)&vmStat, &count) == KERN_SUCCESS)
        {
            long pageSize = sysconf(_SC_PAGESIZE);
            availMem = (vmStat.free_count + vmStat.inactive_count) * pageSize;
        }

#elif __linux__
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open())
        {
            std::string line;
            while (std::getline(meminfo, line))
            {
                if (line.rfind("MemTotal:", 0) == 0)
                {
                    sscanf(line.c_str(), "MemTotal: %llu kB", &totalMem);
                    totalMem *= 1024;
                }
                else if (line.rfind("MemAvailable:", 0) == 0)
                {
                    sscanf(line.c_str(), "MemAvailable: %llu kB", &availMem);
                    availMem *= 1024;
                }
            }
        }
#endif

        if (totalMem > 0)
        {
            unsigned long long usedMem = totalMem - availMem;
            double usedPercent = (double)usedMem / totalMem * 100.0;

            auto toMB = [](unsigned long long bytes) -> double
            {
                return bytes / (1024.0 * 1024.0);
            };

            oss << "Memory: " << toMB(usedMem) << " MB / " << toMB(totalMem)
                << " MB (" << usedPercent << "%)";
        }
        else
        {
            oss << "Memory: unavailable";
        }
    }

    return oss.str();
}

std::vector<std::string_view> extractAllTags(std::string_view text, std::string tag)
{
    std::vector<std::string_view> results;
    size_t pos = 0;

    while (true)
    {
        size_t start_tag_pos = text.find("<" + tag + ">", pos);
        if (start_tag_pos == std::string_view::npos)
            break;

        size_t content_start = start_tag_pos + tag.size() + 2;
        size_t end_tag_pos = text.find("</" + tag + ">", content_start);
        if (end_tag_pos == std::string_view::npos)
            break;

        size_t content_length = end_tag_pos - content_start;
        results.push_back(text.substr(content_start, content_length));

        pos = end_tag_pos + tag.size() + 3;
    }

    return results;
}

std::pair<std::string_view, std::string_view> parseArgs(std::string_view input, char c = ':')
{
    size_t colonPos = input.find(c);
    if (colonPos == std::string_view::npos)
    {
        return {input, {}};
    }
    else
    {
        std::string_view name = input.substr(0, colonPos);
        std::string_view args = input.substr(colonPos + 1);
        return {name, args};
    }
}

std::vector<std::string> get_all_files(const std::string &root_dir)
{
    std::vector<std::string> files;
    if (!std::filesystem::exists(root_dir) || !std::filesystem::is_directory(root_dir))
        throw std::runtime_error("Directory does not exist: " + root_dir);

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root_dir))
    {
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    return files;
}

auto file_parse(const std::string &file_path)
{
    std::filesystem::path p(file_path);
    return std::make_pair(p.stem().string(), p.extension().string());
}

std::vector<std::string> splitString(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
        tokens.push_back(token);
    return tokens;
}

std::vector<std::string> splitEscaped(const std::string &s, char delimiter = '|')
{
    std::vector<std::string> tokens;
    std::string current;
    bool escape = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (escape)
        {
            current.push_back(c);
            escape = false;
        }
        else if (c == '\\')
            escape = true;
        else if (c == delimiter)
        {
            tokens.push_back(std::move(current));
            current.clear();
        }
        else
            current.push_back(c);
    }
    tokens.push_back(std::move(current));
    return tokens;
}

namespace net_unit
{
    void CURL_proxy(CURL *__handle__, const char *proxy, bool ssl = false)
    {
        curl_easy_setopt(__handle__, CURLOPT_PROXY, proxy);
        curl_easy_setopt(__handle__, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        if (ssl)
            curl_easy_setopt(__handle__, CURLOPT_HTTPPROXYTUNNEL, 1L);
        curl_easy_setopt(__handle__, CURLOPT_PROXYUSERPWD, "user:password");
        curl_easy_setopt(__handle__, CURLOPT_PROXYAUTH, CURLAUTH_BASIC);
    }

    size_t CURL_WriteCallback(void *contents, size_t size, size_t nmemb, std::string &userp)
    {
        userp.append((char *)contents, size * nmemb);
        return size * nmemb;
    }

    bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, std::string header = "")
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        struct curl_slist *headers = nullptr;
        if (!header.empty())
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, const std::vector<std::string> &header_list)
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        struct curl_slist *headers = nullptr;
        for (auto &header : header_list)
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, std::string header = "")
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

        struct curl_slist *headers = nullptr;
        if (!header.empty())
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, const std::vector<std::string> &header_list)
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

        struct curl_slist *headers = nullptr;
        for (auto &header : header_list)
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    using StreamCallback = std::function<void(const char *, size_t)>;
    size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        StreamCallback *callback = (StreamCallback *)userdata;
        try
        {
            (*callback)(ptr, size *nmemb);
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
        return size * nmemb;
    }

    bool CURL_stream_post(CURL *curl, const char *url, const std::string &post_data, const std::string &header, StreamCallback on_token)
    {
        if (!curl || !url)
            return false;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        struct curl_slist *header_list = nullptr;
        header_list = curl_slist_append(header_list, header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(header_list);
        return (res == CURLE_OK);
    }
} // namespace net_unit

// Forward declaration for run_unit functions used by tool_unit
namespace run_unit
{
    void mark_file_used(const std::string &path);
    std::vector<std::string> get_used_files();
}

namespace tool_unit
{
    std::string exec(const std::string &cmd)
    {
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
            return "popen failed";

        std::string result;
        int ch;
        while ((ch = fgetc(pipe)) != EOF)
            result += static_cast<char>(ch);
        int code = pclose(pipe);
        printf("INFO - exec(\"%s\") exit-code: %d\n", cmd.c_str(), code);
        return result;
    }

    std::string readFile(const std::string &path)
    {
        std::cout << "INFO - readFile('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + " Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        return content;
    }

    void writeFile(const std::string &path, const std::string &content)
    {
        std::cout << "INFO - writeFile('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ofstream file(path, std::ios::binary | std::ios::out);
        if (!file.is_open())
            throw std::runtime_error("Error - Could not create the file:" + path + " Please check the true path");
        file.write(content.data(), content.size());
        if (file.fail())
            throw std::runtime_error("Fail write to file:'" + path + "'" + " Please check the true path");
        file.close();
    }

    std::string wget(const char *URL)
    {
        CURL *curl = curl_easy_init();
        std::string buf;
        net_unit::CURL_get(curl, URL, buf);
        return buf;
    }

    std::string Image(const std::string &path)
    {
        std::cout << "INFO - Image('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + " Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        auto base = base64::to_base64(content);
        return "data:image/jpeg;base64," + base;
    }

    std::list<std::string> image_queue;

    void editFile(const std::string &path, const std::string &args)
    {
        std::string content = readFile(path);
        std::vector<std::string> lines;
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line))
            lines.push_back(line);

        auto tokens = splitEscaped(args, '|');
        if (tokens.empty())
            throw std::runtime_error("edit: missing operation");

        const std::string &op = tokens[0];

        if (op == "replace_line")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit replace_line: need line_number and new_content");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum == 0 || lineNum > lines.size())
                throw std::runtime_error("edit replace_line: line number out of range");
            lines[lineNum - 1] = tokens[2];
        }
        else if (op == "insert_after")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit insert_after: need line_number and content");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum > lines.size())
                throw std::runtime_error("edit insert_after: line number out of range");
            lines.insert(lines.begin() + lineNum, tokens[2]);
        }
        else if (op == "delete_line")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit delete_line: need line_number");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum == 0 || lineNum > lines.size())
                throw std::runtime_error("edit delete_line: line number out of range");
            lines.erase(lines.begin() + lineNum - 1);
        }
        else if (op == "append")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit append: need content");
            lines.push_back(tokens[1]);
        }
        else if (op == "prepend")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit prepend: need content");
            lines.insert(lines.begin(), tokens[1]);
        }
        else if (op == "replace")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit replace: need old_text and new_text");
            std::string oldStr = tokens[1];
            std::string newStr = tokens[2];
            for (auto &l : lines)
            {
                size_t pos = 0;
                while ((pos = l.find(oldStr, pos)) != std::string::npos)
                {
                    l.replace(pos, oldStr.length(), newStr);
                    pos += newStr.length();
                }
            }
        }
        else
        {
            throw std::runtime_error("edit: unknown operation '" + op + "'");
        }

        std::ostringstream oss;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            oss << lines[i];
            if (i != lines.size() - 1)
                oss << '\n';
        }
        writeFile(path, oss.str());
    }
} // namespace tool_unit

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

namespace run_unit
{
    std::string cs_prompt = "";
    nlohmann::json settings;
    nlohmann::json tools_list;
    std::string setting_file_path;

    // 文件使用跟踪（单线程异步事件循环，无锁访问）
    std::unordered_set<std::string> used_files;

    void mark_file_used(const std::string &path)
    {
        used_files.insert(path);
        if (used_files.size() > 100)
            used_files.erase(used_files.begin());
    }

    std::vector<std::string> get_used_files()
    {
        return {used_files.begin(), used_files.end()};
    }

    class DataManager
    {
    private:
        std::string data_file;

    public:
        nlohmann::json data = {};
        DataManager() = default;
        DataManager(const std::string &workspace) : data_file(workspace + "/sys/data.json")
        {
            try
            {
                data = nlohmann::json::parse(tool_unit::readFile(data_file));
                if (!data.contains("usages"))
                {
                    data["usages"] = nlohmann::json();
                    data["usages"]["memory"] = {
                        {"prompt_cost", 0},
                        {"completion_cost", 0},
                        {"total_cost", 0}};
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        ~DataManager()
        {
            tool_unit::writeFile(data_file, data.dump(4));
        }
    };

    DataManager agent_data_manager;

    // 从消息中提取 base64 图片到 asset_array，替换为 "#n"，返回替换后的消息（使用 std::swap 零拷贝）
    nlohmann::json extract_images_from_message(const nlohmann::json &msg, nlohmann::json &asset_array)
    {
        if (!msg.contains("content") || !msg["content"].is_array())
            return msg;

        nlohmann::json new_msg = msg;

        for (auto &part : new_msg["content"])
        {
            if (part.is_object() && part.value("type", "") == "image_url")
            {
                std::string *url_ptr = nullptr;
                if (part["image_url"].is_string())
                    url_ptr = &part["image_url"].get_ref<std::string &>();
                else if (part["image_url"].is_object() && part["image_url"].contains("url"))
                    url_ptr = &part["image_url"]["url"].get_ref<std::string &>();

                if (url_ptr && url_ptr->find("data:") == 0)
                {
                    asset_array.push_back("");
                    std::swap(asset_array.back().get_ref<std::string &>(), *url_ptr);
                    part = "#" + std::to_string(asset_array.size() - 1);
                }
            }
        }

        return new_msg;
    }

    // 从 asset_array 恢复 base64 图片到消息中替换 "#n"（使用 std::swap 零拷贝）
    void restore_images_in_message(nlohmann::json &msg, nlohmann::json &asset_array)
    {
        if (!msg.contains("content") || !msg["content"].is_array() || asset_array.empty())
            return;

        for (auto &part : msg["content"])
        {
            if (part.is_string())
            {
                auto &tag = part.get_ref<std::string &>();
                if (tag.size() > 1 && tag[0] == '#')
                {
                    try
                    {
                        int idx = std::stoi(tag.substr(1));
                        if (idx >= 0 && idx < (int)asset_array.size())
                        {
                            nlohmann::json img;
                            img["type"] = "image_url";
                            img["image_url"] = nlohmann::json::object();
                            img["image_url"]["url"] = "";
                            std::swap(img["image_url"]["url"].get_ref<std::string &>(), asset_array[idx].get_ref<std::string &>());
                            part = std::move(img);
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }

    struct SessionContext
    {
        nlohmann::json messages = nlohmann::json::array(); // 每个元素为 {role, content}
        nlohmann::json memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
        size_t last_saved_index = 0;
        bool loaded = false;
        bool thinking = false;
        std::string session_id;

        SessionContext()
        {
            session_id = std::to_string(std::time(nullptr));
        }

        SessionContext(const std::string &session_id) : session_id(session_id) {}

        bool is_memory_empty()
        {
            return memory["keywords"].get_ref<const std::string &>().empty() &&
                   memory["abstracts"].get_ref<const std::string &>().empty();
        }

        std::string summary_query()
        {
            std::time_t t = std::time(nullptr);
            std::string time = std::asctime(std::localtime(&t));
            std::string query;
            if (is_memory_empty())
            {
                query += "Analyze the conversation below to produce both a summary and keywords. Include the current time.\nTime:" + time + "\n\nConversation:\n";
            }
            else
            {
                query += "Update the existing memory with new conversation data. Include the current time.\nTime:" + time;
                query += "\n\nOld Abstracts:\n" + memory["abstracts"].get<std::string>();
                query += "\n\nOld Keywords:\n" + memory["keywords"].get<std::string>();
                query += "\n\nNew conversation messages to incorporate (only new messages since last save):\n";
            }

            for (size_t i = last_saved_index; i < messages.size(); i++)
            {
                auto &msg = messages[i];
                std::string content;
                if (msg.is_object() && msg.contains("content"))
                {
                    if (msg["content"].is_string())
                        content = msg["content"].get<std::string>();
                    else if (msg["content"].is_array())
                    {
                        for (auto &part : msg["content"])
                        {
                            if (part["type"] == "text")
                                content += part["text"].get<std::string>();
                        }
                    }
                }
                else if (msg.is_string())
                {
                    content = msg.get<std::string>();
                }
                query += content + "\n\n";
            }

            query += "Respond with valid JSON only (no other text):\n{\"abstracts\": \"...\", \"keywords\": \"...\"}";
            return query;
        }
    };

    class SessionManager
    {
    public:
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions;
        std::string current_session_id;
        std::string workspace = "";

        struct MemoryCacheEntry
        {
            nlohmann::json data;
            std::filesystem::file_time_type mtime;
        };
        std::unordered_map<std::string, MemoryCacheEntry> memory_cache;

        nlohmann::json get_cached_memory(const std::string &path)
        {
            auto it = memory_cache.find(path);
            auto mtime = std::filesystem::last_write_time(path);
            if (it != memory_cache.end() && it->second.mtime == mtime)
                return it->second.data;
            auto data = nlohmann::json::parse(tool_unit::readFile(path));
            memory_cache[path] = {data, mtime};
            return data;
        }

        void invalidate_cache(const std::string &path)
        {
            memory_cache.erase(path);
        }

        std::shared_ptr<SessionContext> get(const std::string &id)
        {
            auto it = sessions.find(id);
            if (it != sessions.end())
            {
                if (it->second->loaded == false)
                {
                    // 从磁盘加载会话消息
                    try
                    {
                        std::string path = workspace + "/sessions/" + it->second->session_id + ".json";
                        if (std::filesystem::exists(path))
                        {
                            it->second->messages = nlohmann::json::parse(tool_unit::readFile(path));
                            if (!it->second->messages.is_array())
                            {
                                it->second->messages = nlohmann::json::array();
                                std::cout << "WARN - Session " << it->second->session_id << " file is not an array, reset." << std::endl;
                            }
                            // 恢复图片数据
                            nlohmann::json asset_array;
                            std::string asset_path = workspace + "/assets/messages/" + it->second->session_id + ".json";
                            if (std::filesystem::exists(asset_path))
                            {
                                try
                                {
                                    asset_array = nlohmann::json::parse(tool_unit::readFile(asset_path));
                                }
                                catch (...)
                                {
                                    asset_array = nlohmann::json::array();
                                }
                            }
                            for (auto &msg : it->second->messages)
                                restore_images_in_message(msg, asset_array);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << e.what() << '\n';
                    }

                    // 加载记忆
                    auto memory_path = workspace + "/memorys/" + it->second->session_id + ".json";
                    if (std::filesystem::exists(memory_path))
                    {
                        try
                        {
                            it->second->memory = nlohmann::json::parse(tool_unit::readFile(memory_path));
                            if (!it->second->memory.contains("abstracts") || !it->second->memory.contains("keywords"))
                                it->second->memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
                            if (it->second->memory.contains("last_saved_index"))
                                it->second->last_saved_index = it->second->memory["last_saved_index"].get<size_t>();
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << e.what() << '\n';
                        }
                    }
                    it->second->loaded = true;
                }
                return it->second;
            }
            return nullptr;
        }

        std::shared_ptr<SessionContext> create()
        {
            auto session = std::make_shared<SessionContext>();
            sessions[session->session_id] = session;
            current_session_id = session->session_id;
            session->loaded = true;
            return session;
        }

        std::shared_ptr<SessionContext> get_current()
        {
            if (current_session_id.empty())
            {
                auto session = std::make_shared<SessionContext>();
                sessions[session->session_id] = session;
                current_session_id = session->session_id;
                session->loaded = true;
                return session;
            }
            return get(current_session_id);
        }

        std::vector<std::string> list_sessions() const
        {
            std::vector<std::string> result;
            for (const auto &pair : sessions)
                result.push_back(pair.first);
            return result;
        }

        void clear_current()
        {
            auto ses = get_current();
            ses->messages.clear();
            ses->memory.clear();
            ses->memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
            ses->last_saved_index = 0;
        }

        void remove_session(const std::string &id)
        {
            sessions.erase(id);
        }

        void change_session(const std::string &id)
        {
            if (current_session_id == id)
                return;
            if (!current_session_id.empty())
            {
                auto it = sessions.find(current_session_id);
                if (it != sessions.end() && it->second->loaded)
                {
                    nlohmann::json save_msgs = nlohmann::json::array();
                    nlohmann::json asset_array = nlohmann::json::array();
                    for (auto &msg : it->second->messages)
                        save_msgs.push_back(extract_images_from_message(msg, asset_array));
                    if (!asset_array.empty())
                        tool_unit::writeFile(workspace + "/assets/messages/" + current_session_id + ".json",
                                             asset_array.dump(4));
                    tool_unit::writeFile(workspace + "/sessions/" + current_session_id + ".json",
                                         save_msgs.dump(4));
                    it->second->memory["last_saved_index"] = it->second->last_saved_index;
                    tool_unit::writeFile(workspace + "/memorys/" + current_session_id + ".json",
                                         it->second->memory.dump(4));
                    it->second->loaded = false;
                    it->second->messages.clear();
                    it->second->memory.clear();
                }
            }
            current_session_id = id;
        }

        SessionManager() = default;

        void init(const std::string &ws)
        {
            workspace = ws;
            try
            {
                auto load_sessions = get_all_files(workspace + "/sessions");
                for (auto &session : load_sessions)
                {
                    auto [name, ext] = file_parse(session);
                    if (ext == ".json")
                    {
                        auto ses_obj = std::make_shared<SessionContext>(name);
                        sessions[name] = ses_obj;
                        std::cout << "Found session: " << name << std::endl;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }

        ~SessionManager()
        {
            try
            {
                for (auto &ses : sessions)
                {
                    if (ses.second->loaded)
                    {
                        nlohmann::json save_msgs = nlohmann::json::array();
                        nlohmann::json asset_array = nlohmann::json::array();
                        for (auto &msg : ses.second->messages)
                            save_msgs.push_back(extract_images_from_message(msg, asset_array));
                        if (!asset_array.empty())
                            tool_unit::writeFile(workspace + "/assets/messages/" + ses.first + ".json",
                                                 asset_array.dump(4));
                        tool_unit::writeFile(workspace + "/sessions/" + ses.first + ".json",
                                             save_msgs.dump(4));
                        ses.second->memory["last_saved_index"] = ses.second->last_saved_index;
                        tool_unit::writeFile(workspace + "/memorys/" + ses.first + ".json",
                                             ses.second->memory.dump(4));
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    };

    SessionManager agent_session_manager;

    int init_check(const std::string &settings_path)
    {
        setting_file_path = settings_path;
        settings = nlohmann::json::parse(tool_unit::readFile(settings_path));

        if (!settings.contains("workspace"))
            throw std::runtime_error("Error - missing 'workspace' in settings");
        if (!settings.contains("channels"))
            throw std::runtime_error("Error - missing 'channels' in settings");
        if (!settings.contains("model"))
            throw std::runtime_error("Error - missing 'model' in settings");
        if (!settings.contains("prompt"))
            throw std::runtime_error("Error - missing 'prompt' in settings");

        std::filesystem::path workspace = settings["workspace"].get<std::string>();
        if (workspace.empty())
            throw std::runtime_error("Error - workspace path is empty");

        std::filesystem::create_directories(workspace / "sessions");
        std::filesystem::create_directories(workspace / "memorys");
        std::filesystem::create_directories(workspace / "assets" / "messages");
        std::filesystem::create_directories(workspace / "tokens");               // 加密频道 token 存储
        std::filesystem::create_directories(workspace / "tokens" / "providers"); // 加密供应商 API Key 存储
        std::filesystem::create_directories(workspace / "tools");

        std::filesystem::path sysPath = workspace / "sys";
        if (!std::filesystem::exists(sysPath) || !std::filesystem::is_directory(sysPath))
            throw std::runtime_error("Error - sys directory not found or is not a directory. Please check your workspace.");

        for (auto &channel : settings["channels"])
        {
            if (!channel.contains("name") || channel["name"].get<std::string>().empty())
                throw std::runtime_error("Error - channel missing 'name' or name is empty");
            std::string channelName = channel["name"].get<std::string>();
            std::filesystem::path sessionFile = workspace / "sessions" / (channelName + ".json");
            if (!std::filesystem::exists(sessionFile))
                tool_unit::writeFile(sessionFile.string(), "[]");
        }

        std::string model = settings["model"].get<std::string>();
        if (model.empty())
            throw std::runtime_error("Error - model is empty. Please check your settings.");

        std::filesystem::path promptPath = workspace / settings["prompt"].get<std::string>();
        if (promptPath.empty())
            throw std::runtime_error("Error - prompt is empty");
        if (!std::filesystem::exists(promptPath) || !std::filesystem::is_regular_file(promptPath))
            throw std::runtime_error("Error - prompt not found or is not a regular file. Please check your settings.");

        if (!settings.contains("server_address") || settings["server_address"].get<std::string>().empty())
        {
            // 兼容新格式：检查 providers 数组
            if (!settings.contains("providers") || !settings["providers"].is_array() || settings["providers"].empty())
                throw std::runtime_error("Error - missing 'providers' array or 'server_address' in settings");
        }

        std::filesystem::path webui = workspace / "webui.html";
        if (!std::filesystem::exists(webui))
            throw std::runtime_error("Error - webui.html not found. Please check your workspace.");

        std::filesystem::path tools_list_path = workspace / "tools/tools.json";
        if (!std::filesystem::exists(tools_list_path))
        {
            std::cout << "Warning - No tools.json found, creating a new one..." << std::endl;
            tool_unit::writeFile(tools_list_path.string(), "[]");
            tools_list = nlohmann::json::array();
        }
        else
        {
            tools_list = nlohmann::json::parse(tool_unit::readFile(tools_list_path.string()));
        }

        std::filesystem::path mcp_tools_path = workspace / "tools/mcp_tools.json";
        if (!std::filesystem::exists(mcp_tools_path))
        {
            std::cout << "Warning - No mcp_tools.json found, creating a new one..." << std::endl;
            tool_unit::writeFile(mcp_tools_path.string(),
                                 nlohmann::json{{"servers", nlohmann::json::array()},
                                                {"tools", nlohmann::json::array()}}
                                     .dump(4));
        }

        std::filesystem::path cs_prompt_path = sysPath / "cs.txt";
        if (!std::filesystem::exists(cs_prompt_path) || !std::filesystem::is_regular_file(cs_prompt_path))
            throw std::runtime_error("Error - cs.txt not found. Please check your sys directory.");
        else
            cs_prompt = tool_unit::readFile(cs_prompt_path.string());

        std::filesystem::path data_path = sysPath / "data.json";
        if (!std::filesystem::exists(data_path) || !std::filesystem::is_regular_file(data_path))
            throw std::runtime_error("Error - data.json not found. Please check your sys directory.");

        agent_data_manager = DataManager(workspace.string());
        agent_session_manager.init(workspace.string());
        return 0;
    }

    bool validateJsonFormat(const nlohmann::json &j)
    {
        if (!j.contains("user_name") || !j["user_name"].is_string())
        {
            std::cerr << "Error: missing 'name'" << std::endl;
            return false;
        }
        if (!j.contains("agent_name") || !j["agent_name"].is_string())
        {
            std::cerr << "Error: missing 'agent_name'" << std::endl;
            return false;
        }
        if (!j.contains("workspace") || !j["workspace"].is_string())
        {
            std::cerr << "Error: missing 'workspace'" << std::endl;
            return false;
        }
        // 兼容新旧格式：检查 providers 数组或 server_address
        if (!j.contains("providers") && (!j.contains("server_address") || !j["server_address"].is_string()))
        {
            std::cerr << "Error: missing 'providers' array or 'server_address'" << std::endl;
            return false;
        }
        if (!j.contains("model") || !j["model"].is_string())
        {
            std::cerr << "Error: missing 'model'" << std::endl;
            return false;
        }
        if (!j.contains("prompt") || !j["prompt"].is_string())
        {
            std::cerr << "Error: missing 'prompt'" << std::endl;
            return false;
        }
        if (!j.contains("stream") || !j["stream"].is_boolean())
        {
            std::cerr << "Error: missing 'stream'" << std::endl;
            return false;
        }
        if (!j.contains("max_mpc_rounds") || !j["max_mpc_rounds"].is_number_integer())
        {
            std::cerr << "Error: missing 'max_mpc_rounds'" << std::endl;
            return false;
        }
        if (!j.contains("max_context") || !j["max_context"].is_number_integer())
        {
            std::cerr << "Error: missing 'max_context'" << std::endl;
            return false;
        }

        if (!j.contains("channels") || !j["channels"].is_array())
        {
            std::cerr << "Error: missing 'channels'" << std::endl;
            return false;
        }
        const auto &channels = j["channels"];
        for (size_t i = 0; i < channels.size(); ++i)
        {
            const auto &ch = channels[i];
            if (!ch.is_object())
            {
                std::cerr << "Error: channels[" << i << "] not an object" << std::endl;
                return false;
            }
            if (!ch.contains("name") || !ch["name"].is_string())
            {
                std::cerr << "Error: channels[" << i << "] missing 'name'" << std::endl;
                return false;
            }
            if (!ch.contains("status") || !ch["status"].is_string())
            {
                std::cerr << "Error: channels[" << i << "] missing 'status'" << std::endl;
                return false;
            }
            if (!ch.contains("user_count") || !ch["user_count"].is_number_integer())
            {
                std::cerr << "Error: channels[" << i << "] missing 'user_count'" << std::endl;
                return false;
            }
            if (!ch.contains("path") || !ch["path"].is_string())
            {
                std::cerr << "Error: channels[" << i << "] missing 'path'" << std::endl;
                return false;
            }
        }
        return true;
    }
} // run_unit

namespace mcp_unit
{
    using json = nlohmann::json;

    // ==================== MCP stdio 客户端（JSON-RPC 2.0 over stdio） ====================

    struct MCPProcess
    {
        std::string name;
        std::string command;
        std::vector<std::string> args;
        std::map<std::string, std::string> env;
    };

    static long long mcp_now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // 从缓冲区提取一条完整的 JSON-RPC 消息（同时支持换行分隔与 Content-Length 帧格式），
    // 提取成功则消费缓冲区并返回消息，否则不消费缓冲区。
    static std::optional<json> mcp_try_extract(std::string &buf)
    {
        if (buf.rfind("Content-Length:", 0) == 0)
        {
            size_t hdr_end = buf.find("\r\n\r\n");
            if (hdr_end == std::string::npos)
                return std::nullopt;
            size_t len = 0;
            try
            {
                len = std::stoull(buf.substr(15, hdr_end - 15));
            }
            catch (...)
            {
                return std::nullopt;
            }
            size_t body_start = hdr_end + 4;
            if (buf.size() < body_start + len)
                return std::nullopt;
            std::string body = buf.substr(body_start, len);
            buf.erase(0, body_start + len);
            try
            {
                return json::parse(body);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
        size_t nl = buf.find('\n');
        if (nl == std::string::npos)
            return std::nullopt;
        std::string line = buf.substr(0, nl);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
        {
            buf.erase(0, nl + 1);
            return mcp_try_extract(buf);
        }
        json j;
        try
        {
            j = json::parse(line);
        }
        catch (...)
        {
            // 行不完整（可能是半条消息），保留缓冲区等待更多数据
            return std::nullopt;
        }
        buf.erase(0, nl + 1);
        return j;
    }

    class MCPClient
    {
    public:
        explicit MCPClient(const MCPProcess &cfg) : cfg_(cfg) {}
        ~MCPClient() { close(); }

        int timeout_ms = 60000;

        bool open()
        {
            close();
            return spawn_stdio();
        }

        void close();

        bool send(const std::string &s);
        json recv_response(int64_t want_id, int timeout_ms);

        json request(int64_t id, const std::string &method, const json &params)
        {
            json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
            send(msg.dump());
            return recv_response(id, timeout_ms);
        }

        void notify(const std::string &method, const json &params)
        {
            json msg = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
            send(msg.dump());
        }

        // 握手 + 列出远程工具
        json initialize()
        {
            json params = {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {}},
                {"clientInfo", {{"name", "agent.cpp"}, {"version", "1.0"}}}};
            return request(1, "initialize", params);
        }

        json list_tools() { return request(2, "tools/list", json::object()); }

        json call_tool(const std::string &tool, const json &arguments)
        {
            json params = {{"name", tool}, {"arguments", arguments}};
            return request(3, "tools/call", params);
        }

    private:
        MCPProcess cfg_;
        std::string buffer_;
#ifdef _WIN32
        HANDLE h_proc_ = nullptr;
        HANDLE h_in_ = nullptr;  // 写入子进程 stdin
        HANDLE h_out_ = nullptr; // 读取子进程 stdout
#else
        pid_t pid_ = -1;
        int in_fd_ = -1;
        int out_fd_ = -1;
#endif
        bool spawn_stdio();
        bool read_more(int timeout_ms);
    };

#ifdef _WIN32
    // ==================== Windows 实现 ====================

    static std::wstring mcp_utf8_to_wide(const std::string &s)
    {
        if (s.empty())
            return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
        if (!w.empty() && w.back() == L'\0')
            w.pop_back();
        return w;
    }

    static std::wstring mcp_build_env_block(const std::map<std::string, std::string> &env)
    {
        std::map<std::wstring, std::wstring> merged;
        LPWCH cur = GetEnvironmentStringsW();
        if (cur)
        {
            for (LPWCH p = cur; *p; p += wcslen(p) + 1)
            {
                std::wstring s(p);
                size_t eq = s.find(L'=');
                if (eq != std::wstring::npos)
                    merged[s.substr(0, eq)] = s.substr(eq + 1);
            }
            FreeEnvironmentStringsW(cur);
        }
        for (auto &[k, v] : env)
            merged[mcp_utf8_to_wide(k)] = mcp_utf8_to_wide(v);
        std::wstring block;
        for (auto &[k, v] : merged)
        {
            block += k + L"=" + v + L'\0';
        }
        block += L'\0';
        return block;
    }

    bool MCPClient::spawn_stdio()
    {
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE stdin_read = nullptr, stdin_write = nullptr;
        HANDLE stdout_read = nullptr, stdout_write = nullptr;
        if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0))
            return false;
        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
        {
            CloseHandle(stdin_read);
            CloseHandle(stdin_write);
            return false;
        }
        SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

        // 通过 cmd /c 启动（兼容 .cmd/.bat 脚本如 npx.cmd），外层引号让 cmd 正确解析
        std::wstring cmdline = L"cmd /c \"";
        cmdline += L"\"" + mcp_utf8_to_wide(cfg_.command) + L"\"";
        for (auto &a : cfg_.args)
            cmdline += L" \"" + mcp_utf8_to_wide(a) + L"\"";
        cmdline += L"\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = stdin_read;
        si.hStdOutput = stdout_write;
        si.hStdError = stdout_write;

        PROCESS_INFORMATION pi{};
        std::wstring env_block;
        LPWCH env_ptr = nullptr;
        if (!cfg_.env.empty())
        {
            env_block = mcp_build_env_block(cfg_.env);
            env_ptr = &env_block[0];
        }

        std::wstring wcmd = cmdline;
        BOOL ok = CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE, 0,
                                 env_ptr, nullptr, &si, &pi);
        CloseHandle(stdin_read);
        CloseHandle(stdout_write);
        if (!ok)
        {
            CloseHandle(stdin_write);
            CloseHandle(stdout_read);
            return false;
        }
        h_proc_ = pi.hProcess;
        CloseHandle(pi.hThread);
        h_in_ = stdin_write;
        h_out_ = stdout_read;
        return true;
    }

    bool MCPClient::send(const std::string &s)
    {
        if (!h_in_)
            return false;
        std::string msg = s + "\n";
        DWORD written = 0;
        size_t total = 0;
        while (total < msg.size())
        {
            if (!WriteFile(h_in_, msg.data() + total, (DWORD)(msg.size() - total), &written, nullptr))
                return false;
            total += written;
        }
        return true;
    }

    bool MCPClient::read_more(int timeout_ms)
    {
        DWORD avail = 0;
        if (!PeekNamedPipe(h_out_, nullptr, 0, nullptr, &avail, nullptr))
            return false;
        if (avail > 0)
        {
            std::string chunk(avail, '\0');
            DWORD read_bytes = 0;
            if (!ReadFile(h_out_, &chunk[0], avail, &read_bytes, nullptr))
                return false;
            chunk.resize(read_bytes);
            buffer_ += chunk;
            return true;
        }
        if (h_proc_ && WaitForSingleObject(h_proc_, 0) == WAIT_OBJECT_0)
            return false; // 子进程已退出
        Sleep(std::max(1, std::min(10, timeout_ms)));
        return true;
    }

    void MCPClient::close()
    {
        if (h_in_)
        {
            CloseHandle(h_in_);
            h_in_ = nullptr;
        }
        if (h_out_)
        {
            CloseHandle(h_out_);
            h_out_ = nullptr;
        }
        if (h_proc_)
        {
            WaitForSingleObject(h_proc_, 500);
            TerminateProcess(h_proc_, 1);
            CloseHandle(h_proc_);
            h_proc_ = nullptr;
        }
    }

#else
    // ==================== POSIX 实现 ====================

    bool MCPClient::spawn_stdio()
    {
        int in_pipe[2] = {-1, -1}, out_pipe[2] = {-1, -1};
        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
            return false;
        pid_t pid = fork();
        if (pid < 0)
        {
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            return false;
        }
        if (pid == 0)
        {
            dup2(in_pipe[0], STDIN_FILENO);
            dup2(out_pipe[1], STDOUT_FILENO);
            dup2(out_pipe[1], STDERR_FILENO);
            ::close(in_pipe[0]);
            ::close(in_pipe[1]);
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            for (auto &[k, v] : cfg_.env)
                setenv(k.c_str(), v.c_str(), 1);
            std::vector<char *> argv;
            argv.push_back(const_cast<char *>(cfg_.command.c_str()));
            for (auto &a : cfg_.args)
                argv.push_back(const_cast<char *>(a.c_str()));
            argv.push_back(nullptr);
            execvp(cfg_.command.c_str(), argv.data());
            _exit(127);
        }
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        pid_ = pid;
        in_fd_ = in_pipe[1];
        out_fd_ = out_pipe[0];
        return true;
    }

    bool MCPClient::send(const std::string &s)
    {
        if (in_fd_ < 0)
            return false;
        std::string msg = s + "\n";
        size_t total = 0;
        while (total < msg.size())
        {
            ssize_t n = write(in_fd_, msg.data() + total, msg.size() - total);
            if (n <= 0)
                return false;
            total += (size_t)n;
        }
        return true;
    }

    bool MCPClient::read_more(int timeout_ms)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(out_fd_, &fds);
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int rc = select(out_fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (rc <= 0)
        {
            if (rc == 0)
            {
                int status = 0;
                if (waitpid(pid_, &status, WNOHANG) == pid_)
                    return false; // 子进程已退出
            }
            return true; // 超时，继续等待
        }
        char chunk[4096];
        ssize_t n = read(out_fd_, chunk, sizeof(chunk));
        if (n <= 0)
            return false;
        buffer_.append(chunk, (size_t)n);
        return true;
    }

    void MCPClient::close()
    {
        if (in_fd_ >= 0)
        {
            ::close(in_fd_);
            in_fd_ = -1;
        }
        if (out_fd_ >= 0)
        {
            ::close(out_fd_);
            out_fd_ = -1;
        }
        if (pid_ > 0)
        {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
    }
#endif

    json MCPClient::recv_response(int64_t want_id, int timeout_ms)
    {
        long long deadline = mcp_now_ms() + timeout_ms;
        while (mcp_now_ms() < deadline)
        {
            auto msg = mcp_try_extract(buffer_);
            if (msg)
            {
                if (msg->contains("id") && (*msg)["id"] == want_id)
                {
                    if (msg->contains("error"))
                        throw std::runtime_error("MCP JSON-RPC error: " + (*msg)["error"].dump());
                    return (*msg)["result"];
                }
                continue; // 通知或其它 id 的消息，忽略
            }
            if (!read_more((int)std::max<long long>(1, deadline - mcp_now_ms())))
                break; // EOF
        }
        // 兜底：服务器未换行终止时的残留数据
        auto msg = mcp_try_extract(buffer_);
        if (msg && msg->contains("id") && (*msg)["id"] == want_id)
        {
            if (msg->contains("error"))
                throw std::runtime_error("MCP JSON-RPC error: " + (*msg)["error"].dump());
            return (*msg)["result"];
        }
        throw std::runtime_error("MCP timeout or process exited while waiting for response (id=" +
                                 std::to_string(want_id) + ")");
    }

    // ==================== MCP HTTP 客户端（libcurl / MCP Streamable HTTP 传输） ====================

    class MCPHTTPClient
    {
    public:
        MCPHTTPClient(const std::string &url, const std::map<std::string, std::string> &headers)
            : url_(url), headers_(headers) {}

        int timeout_ms = 120000;

        json initialize()
        {
            json params = {
                {"protocolVersion", "2025-03-26"},
                {"capabilities", {}},
                {"clientInfo", {{"name", "agent.cpp"}, {"version", "1.0"}}}};
            return request(1, "initialize", params);
        }

        json list_tools() { return request(2, "tools/list", json::object()); }

        json call_tool(const std::string &tool, const json &arguments)
        {
            json params = {{"name", tool}, {"arguments", arguments}};
            return request(3, "tools/call", params);
        }

        void notify(const std::string &method, const json &params)
        {
            json msg = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
            try
            {
                post(msg);
            }
            catch (...)
            {
            } // 服务器可能以 202 + 空响应确认通知
        }

        json request(int64_t id, const std::string &method, const json &params)
        {
            json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
            json resp = post(msg);
            if (resp.contains("error"))
                throw std::runtime_error("MCP JSON-RPC error: " + resp["error"].dump());
            if (!resp.contains("result"))
                throw std::runtime_error("MCP JSON-RPC response missing result: " + resp.dump());
            return resp["result"];
        }

    private:
        std::string url_;
        std::map<std::string, std::string> headers_;
        std::string session_id_;

        // 捕获 Mcp-Session-Id 响应头，后续请求回传以维持会话
        static size_t header_cb(void *contents, size_t size, size_t nmemb, void *userdata)
        {
            MCPHTTPClient *self = static_cast<MCPHTTPClient *>(userdata);
            std::string line((char *)contents, size * nmemb);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string name = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                    value.erase(value.begin());
                while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                    value.pop_back();
                for (auto &c : name)
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                if (name == "mcp-session-id")
                    self->session_id_ = value;
            }
            return size * nmemb;
        }

        // 响应体可能是纯 JSON 或 SSE 流（text/event-stream）
        static json parse_response(const std::string &buf)
        {
            try
            {
                return json::parse(buf);
            }
            catch (...)
            {
            }
            size_t pos = 0;
            while (pos < buf.size())
            {
                size_t nl = buf.find('\n', pos);
                std::string line = buf.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                pos = nl == std::string::npos ? buf.size() : nl + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.rfind("data:", 0) == 0)
                {
                    std::string payload = line.substr(5);
                    if (!payload.empty() && payload.front() == ' ')
                        payload.erase(payload.begin());
                    if (!payload.empty())
                    {
                        try
                        {
                            return json::parse(payload);
                        }
                        catch (...)
                        {
                        }
                    }
                }
            }
            throw std::runtime_error("MCP: failed to parse HTTP response");
        }

        json post(const json &msg)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
                throw std::runtime_error("curl_easy_init failed");
            std::string body = msg.dump();
            std::string buf;
            struct curl_slist *hdr = nullptr;
            hdr = curl_slist_append(hdr, "Content-Type: application/json");
            hdr = curl_slist_append(hdr, "Accept: application/json, text/event-stream");
            if (!session_id_.empty())
                hdr = curl_slist_append(hdr, ("Mcp-Session-Id: " + session_id_).c_str());
            for (auto &[k, v] : headers_)
                hdr = curl_slist_append(hdr, (k + ": " + v).c_str());

            curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地 MCP 服务器不走系统代理
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, net_unit::CURL_WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, this);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)std::max(1, timeout_ms / 1000));

            CURLcode res = curl_easy_perform(curl);
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            curl_slist_free_all(hdr);
            curl_easy_cleanup(curl);
            if (res != CURLE_OK)
                throw std::runtime_error(std::string("MCP HTTP request failed: ") + curl_easy_strerror(res));
            if (code >= 400)
                throw std::runtime_error("MCP HTTP request failed (HTTP " + std::to_string(code) + "): " + buf);
            return parse_response(buf);
        }
    };

    // ==================== 配置持久化：workspace/tools/mcp_tools.json ====================

    inline std::string config_path()
    {
        return run_unit::settings["workspace"].get_ref<const std::string &>() + "/tools/mcp_tools.json";
    }

    inline json load_config()
    {
        if (std::filesystem::exists(config_path()))
            return json::parse(tool_unit::readFile(config_path()));
        json cfg = {{"servers", json::array()}, {"tools", json::array()}};
        tool_unit::writeFile(config_path(), cfg.dump(4));
        return cfg;
    }

    inline void save_config(const json &cfg)
    {
        tool_unit::writeFile(config_path(), cfg.dump(4));
    }

    inline json list_servers() { return load_config()["servers"]; }
    inline json list_mapped_tools() { return load_config()["tools"]; }

    inline const json *find_server(const json &cfg, const std::string &name)
    {
        for (const auto &s : cfg["servers"])
            if (s.value("name", "") == name)
                return &s;
        return nullptr;
    }

    inline MCPProcess to_process(const json &server)
    {
        MCPProcess p;
        p.name = server.value("name", "");
        p.command = server.value("command", "");
        for (auto &a : server.value("args", json::array()))
            if (a.is_string())
                p.args.push_back(a.get<std::string>());
        if (server.contains("env") && server["env"].is_object())
            for (auto &[k, v] : server["env"].items())
                if (v.is_string())
                    p.env[k] = v.get<std::string>();
        return p;
    }

    inline std::map<std::string, std::string> mcp_http_headers(const json &server)
    {
        std::map<std::string, std::string> headers;
        if (server.contains("headers") && server["headers"].is_object())
            for (auto &[k, v] : server["headers"].items())
                if (v.is_string())
                    headers[k] = v.get<std::string>();
        return headers;
    }

    // 连接 MCP 服务器并返回远程工具列表
    inline json list_remote_tools(const std::string &server_name, int timeout_ms = 120000)
    {
        auto cfg = load_config();
        const json *server = find_server(cfg, server_name);
        if (!server)
            throw std::runtime_error("MCP server not found: " + server_name);
        std::string url = server->value("url", "");
        if (!url.empty())
        {
            // Streamable HTTP 传输（libcurl）
            MCPHTTPClient client(url, mcp_http_headers(*server));
            client.timeout_ms = timeout_ms;
            client.initialize();
            client.notify("notifications/initialized", json::object());
            json result = client.list_tools();
            return result.value("tools", json::array());
        }
        if (server->value("command", "").empty())
            throw std::runtime_error("MCP server command is empty: " + server_name);
        MCPClient client(to_process(*server));
        client.timeout_ms = timeout_ms;
        if (!client.open())
            throw std::runtime_error("Failed to start MCP server: " + server->value("command", ""));
        client.initialize();
        client.notify("notifications/initialized", json::object());
        json result = client.list_tools();
        return result.value("tools", json::array());
    }

    // ---------- 服务器 / 映射管理 ----------

    inline void add_server(const json &server)
    {
        std::string name = server.value("name", "");
        if (name.empty())
            throw std::runtime_error("MCP server name is required");
        if (server.value("command", "").empty() && server.value("url", "").empty())
            throw std::runtime_error("MCP server command or url is required");
        auto cfg = load_config();
        for (auto &s : cfg["servers"])
        {
            if (s.value("name", "") == name)
            {
                s = server;
                save_config(cfg);
                return;
            }
        }
        cfg["servers"].push_back(server);
        save_config(cfg);
    }

    inline void delete_server(const std::string &name)
    {
        auto cfg = load_config();
        cfg["servers"].erase(
            std::remove_if(cfg["servers"].begin(), cfg["servers"].end(),
                           [&](const json &s)
                           { return s.value("name", "") == name; }),
            cfg["servers"].end());
        // 同时移除该服务器的所有映射
        cfg["tools"].erase(
            std::remove_if(cfg["tools"].begin(), cfg["tools"].end(),
                           [&](const json &t)
                           { return t.value("server", "") == name; }),
            cfg["tools"].end());
        save_config(cfg);
    }

    inline void add_mapping(const json &mapping)
    {
        std::string name = mapping.value("name", "");
        if (name.empty())
            throw std::runtime_error("Mapped tool name is required");
        if (mapping.value("server", "").empty())
            throw std::runtime_error("MCP server name is required");
        auto cfg = load_config();
        for (auto &t : cfg["tools"])
        {
            if (t.value("name", "") == name)
            {
                t = mapping;
                save_config(cfg);
                return;
            }
        }
        cfg["tools"].push_back(mapping);
        save_config(cfg);
    }

    inline void delete_mapping(const std::string &name)
    {
        auto cfg = load_config();
        cfg["tools"].erase(
            std::remove_if(cfg["tools"].begin(), cfg["tools"].end(),
                           [&](const json &t)
                           { return t.value("name", "") == name; }),
            cfg["tools"].end());
        save_config(cfg);
    }

    inline void set_mapping_enabled(const std::string &name, bool enabled)
    {
        auto cfg = load_config();
        for (auto &t : cfg["tools"])
        {
            if (t.value("name", "") == name)
            {
                t["enabled"] = enabled;
                save_config(cfg);
                return;
            }
        }
        throw std::runtime_error("MCP tool not mapped: " + name);
    }

    inline bool is_mcp_tool(const std::string &name)
    {
        auto cfg = load_config();
        for (auto &t : cfg["tools"])
            if (t.value("name", "") == name)
                return true;
        return false;
    }

    // 本地工具 + 已映射的 MCP 工具合并列表（for_prompt=true 时附带参数 JSON schema）
    inline json merged_tools_list(bool for_prompt = false)
    {
        json result = json::array();
        for (auto &t : run_unit::tools_list)
            result.push_back(t);
        auto cfg = load_config();
        for (auto &t : cfg["tools"])
        {
            json entry = t;
            entry["mcp"] = true;
            if (for_prompt && entry.contains("schema") && entry["schema"].is_object())
            {
                std::string desc = entry.value("description", "");
                if (!desc.empty())
                    desc += " | ";
                desc += "Arguments (JSON object): " + entry["schema"].dump();
                entry["description"] = desc;
            }
            result.push_back(entry);
        }
        return result;
    }

    // 供 LLM 提示词使用的已启用工具列表字符串
    inline std::string enabled_tools_prompt_str()
    {
        json arr = json::array();
        for (auto &t : merged_tools_list(true))
            if (t.value("enabled", true))
                arr.push_back(t);
        return arr.dump();
    }

    // 代理调用：<tool>name:{json args}</tool> → MCP tools/call
    inline std::string call_mapped_tool(const std::string &name, const std::string &args_str)
    {
        auto cfg = load_config();
        const json *mapping = nullptr;
        for (auto &t : cfg["tools"])
            if (t.value("name", "") == name)
            {
                mapping = &t;
                break;
            }
        if (!mapping)
            throw std::runtime_error("MCP tool not mapped: " + name);
        const json *server = find_server(cfg, mapping->value("server", ""));
        if (!server)
            throw std::runtime_error("MCP server not found: " + mapping->value("server", ""));

        json arguments = json::object();
        std::string trimmed = args_str;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' ||
                                    trimmed.front() == '\r' || trimmed.front() == '\n'))
            trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                                    trimmed.back() == '\r' || trimmed.back() == '\n'))
            trimmed.pop_back();
        if (!trimmed.empty())
        {
            bool parsed = false;
            try
            {
                json j = json::parse(trimmed);
                if (j.is_object())
                {
                    arguments = j;
                    parsed = true;
                }
            }
            catch (...)
            {
            }
            if (!parsed)
                arguments = {{"input", args_str}};
        }

        json result;
        std::string url = server->value("url", "");
        if (!url.empty())
        {
            // Streamable HTTP 传输（libcurl）
            MCPHTTPClient client(url, mcp_http_headers(*server));
            client.timeout_ms = 120000;
            client.initialize();
            client.notify("notifications/initialized", json::object());
            result = client.call_tool(mapping->value("tool", name), arguments);
        }
        else
        {
            MCPClient client(to_process(*server));
            client.timeout_ms = 120000; // 首次 npx 拉取依赖可能较慢
            if (!client.open())
                throw std::runtime_error("Failed to start MCP server: " + server->value("command", ""));
            client.initialize();
            client.notify("notifications/initialized", json::object());
            result = client.call_tool(mapping->value("tool", name), arguments);
        }
        std::string out;
        bool is_err = result.value("isError", false);
        if (is_err)
            out += "[MCP_TOOL_ERROR]\n";
        if (result.contains("content") && result["content"].is_array())
        {
            for (auto &item : result["content"])
            {
                if (item.is_object() && item.contains("text") && item["text"].is_string())
                    out += item["text"].get<std::string>() + "\n";
                else if (item.is_object())
                    out += item.dump() + "\n";
            }
        }
        else
        {
            out += result.dump();
        }
        return out;
    }
} // namespace mcp_unit

namespace tool_unit
{
    std::pair<size_t, size_t> tools_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "tool");
        size_t count = 0;
        size_t succeed = 0;
        if (arr.size() < 1)
            return {count, succeed};

        for (auto &ctx : arr)
        {
            data += "\n<system_output>\n";
            auto [name, args] = parseArgs(ctx);
            bool tool_ok = false;
            if (name == "exec")
            {
                try
                {
                    data += exec(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "read")
            {
                try
                {
                    data += readFile(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "Image")
            {
                try
                {
                    image_queue.push_back(Image(std::string(args)));
                    data += "[Image has read done]";
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "write")
            {
                try
                {
                    auto [file, content] = parseArgs(args, '|');
                    writeFile(std::string(file), std::string(content));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "wget")
            {
                try
                {
                    data += wget(std::string(args).c_str());
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "edit")
            {
                try
                {
                    auto [file, rest] = parseArgs(args, '|');
                    editFile(std::string(file), std::string(rest));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else
            {
                try
                {
                    if (mcp_unit::is_mcp_tool(std::string(name)))
                    {
                        // MCP 工具：将参数作为 JSON 对象转发给 MCP 服务器
                        data += mcp_unit::call_mapped_tool(std::string(name), std::string(args));
                    }
                    else
                    {
#ifdef _WIN32
                        data += exec("cmd /c chcp 65001>nul && python.exe " +
                                     run_unit::settings["workspace"].get_ref<const std::string &>() +
                                     "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#else
                        data += exec("python3 " +
                                     run_unit::settings["workspace"].get_ref<const std::string &>() +
                                     "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#endif //_WIN32
                    }

                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            if (tool_ok)
                succeed += 1;
            else
                data += "Tool execution error, please check that you're using it correctly and that the parameters are correct!";
            data += "\n</system_output>\n";
        }
        return {count, succeed};
    }
} // namespace tool_unit

namespace cs_unit
{
    struct Command
    {
        std::string name;
        std::function<std::string(std::string_view)> callback;
    };

    static std::vector<Command> command_map = {
        {"system_status", [](std::string_view args) -> std::string
         {
             try
             {
                 return get_system_status();
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"tools", [](std::string_view args) -> std::string
         {
             try
             {
                 return tool_unit::readFile(run_unit::settings["workspace"].get_ref<const std::string &>() + "/tools/tools.json");
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"restart", [](std::string_view args) -> std::string
         { return "Restart OK"; }},
        {"time", [](std::string_view args) -> std::string
         {
             try
             {
                 std::time_t t = std::time(nullptr);
                 return std::asctime(std::localtime(&t));
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"random", [](std::string_view args) -> std::string
         {
             try
             {
                 int seed;
                 sscanf(args.data(), "%d", &seed);
                 std::mt19937 gen(seed);
                 std::uniform_int_distribution<int> dist(-1e9, 1e9);
                 return std::to_string(dist(gen));
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"mem-keys", [](std::string_view args) -> std::string
         {
             try
             {
                 std::string keys;
                 std::string mem_dir = run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys";
                 auto &mgr = run_unit::agent_session_manager;
                 auto load_sessions = get_all_files(mem_dir);
                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json")
                     {
                         nlohmann::json memory = mgr.get_cached_memory(session);
                         if (memory.contains("abstracts") && memory.contains("keywords"))
                             if (!memory["keywords"].get_ref<const std::string &>().empty() && !memory["abstracts"].get_ref<const std::string &>().empty())
                                 keys += name + ": " + memory["keywords"].get_ref<const std::string &>() + "\n";
                     }
                 }
                 return keys;
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"mem-get", [](std::string_view args) -> std::string
         {
             try
             {
                 std::string query;
                 std::string mem_dir = run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys";
                 auto &mgr = run_unit::agent_session_manager;
                 auto load_sessions = get_all_files(mem_dir);
                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json" && args.find(name) != std::string::npos)
                     {
                         nlohmann::json memory = mgr.get_cached_memory(session);
                         if (memory.contains("abstracts") && memory.contains("keywords"))
                             query += name + ": " + memory["abstracts"].get_ref<const std::string &>() + "\n";
                     }
                 }
                 return query;
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }}};

    // 同样使用 <system_output> 包裹cs输出
    size_t cs_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "cs");
        size_t count = 0;
        if (arr.size() < 1)
            return count;

        for (auto &tag : arr)
        {
            data += "<system_output>\n";
            auto [name, args] = parseArgs(tag);
            for (auto &cmd : command_map)
            {
                if (cmd.name == name)
                {
                    data += cmd.callback(args);
                    data += "\n[CS_DONE]\n";
                    count += 1;
                }
            }
            data += "\n</system_output>\n";
        }
        return count;
    }
} // namespace cs_unit

namespace LLMProviders
{
    class LlamaClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit LlamaClient(const std::string &base_url = "http://localhost:8080", const std::string &api_key = "")
            : base_url_(base_url), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = base_url; }
        ~LlamaClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        bool unload_model(std::string &model)
        {
            std::string buf;
            std::string url = base_url_ + "/models/unload";
            nlohmann::json request = {{"model", model}};
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, "Content-Type: application/json"))
                return false;
            return true;
        }

        bool generate(const nlohmann::json &request, nlohmann::json &response)
        {
            std::string buf;
            std::string url = base_url_ + "/chat/completions";
            std::string header = "Content-Type: application/json";
            if (!api_key_.empty())
                header = "Authorization: Bearer " + api_key_.str() + "\r\nContent-Type: application/json";
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, header))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::string models()
        {
            std::string result;
            std::string url = base_url_ + "/v1/models";
            std::string header = "Content-Type: application/json";
            if (!api_key_.empty())
                header = "Authorization: Bearer " + api_key_.str() + "\r\nContent-Type: application/json";
            net_unit::CURL_get(curl_easy_init(), url.c_str(), result, header);
            return result;
        }
    };

    class OllamaClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit OllamaClient(const std::string &base_url = "http://localhost:11434", const std::string &api_key = "")
            : base_url_(base_url), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = base_url; }
        ~OllamaClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        bool generate(const nlohmann::json &request, nlohmann::json &response)
        {
            std::string buf;
            std::string url = base_url_ + "/v1/chat/completions";
            std::string header = "Content-Type: application/json";
            if (!api_key_.empty())
                header = "Authorization: Bearer " + api_key_.str() + "\r\nContent-Type: application/json";
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, header))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::string models()
        {
            std::string result;
            std::string url = base_url_ + "/api/tags";
            net_unit::CURL_get(curl_easy_init(), url.c_str(), result);
            nlohmann::json ollama_models = nlohmann::json::parse(result);
            nlohmann::json data = nlohmann::json::array();
            for (const auto &item : ollama_models["models"])
            {
                nlohmann::json id = {"id", item["model"]};
                nlohmann::json status = {"status", {{"value", "loaded"}}};
                data.push_back({id, status});
            }
            return nlohmann::json({{"data", data}}).dump();
        }
    };

    class OpenAIClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit OpenAIClient(const std::string &base_url = "http://localhost:11434", const std::string &api_key = "")
            : base_url_(base_url), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = base_url; }
        ~OpenAIClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        bool generate(nlohmann::json &request, nlohmann::json &response)
        {
            std::string buf;
            std::string url = base_url_ + "/v1/chat/completions";
            auto user_name = run_unit::settings["user_name"].get_ref<const std::string &>();
            auto agent_name = run_unit::settings["agent_name"].get_ref<const std::string &>();

            for (auto &msg : request["messages"])
            {
                auto name = msg["role"].get_ref<std::string &>();
                if (name == user_name)
                    msg["role"] = "user";
                else if (name == agent_name)
                    msg["role"] = "assistant";
            }

            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf,
                                     {"Authorization: Bearer " + api_key_.str(), "Content-Type: application/json"}))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // 流式生成：on_token 接收内容增量，on_thinking 接收推理内容增量，返回完整累积文本
        std::string stream_generate(
            nlohmann::json &request,
            std::function<void(const std::string &)> on_token,
            std::function<void(const std::string &)> on_thinking = nullptr)
        {
            request["stream"] = true;
            std::string url = base_url_ + "/v1/chat/completions";
            auto user_name = run_unit::settings["user_name"].get_ref<const std::string &>();
            auto agent_name = run_unit::settings["agent_name"].get_ref<const std::string &>();

            for (auto &msg : request["messages"])
            {
                auto name = msg["role"].get_ref<std::string &>();
                if (name == user_name)
                    msg["role"] = "user";
                else if (name == agent_name)
                    msg["role"] = "assistant";
            }

            std::string header = "Content-Type: application/json";
            if (!api_key_.empty())
                header = "Authorization: Bearer " + api_key_.str() + "\r\nContent-Type: application/json";

            std::string accumulated;
            std::string buffer;

            auto parse_sse = [&](const char *data, size_t len)
            {
                buffer.append(data, len);
                size_t pos = 0;
                while (true)
                {
                    size_t nl = buffer.find('\n', pos);
                    if (nl == std::string::npos)
                        break;
                    std::string line = buffer.substr(pos, nl - pos);
                    pos = nl + 1;

                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                    if (line.empty())
                        continue;

                    if (line.find("data: ") == 0)
                    {
                        std::string payload = line.substr(6);
                        if (payload == "[DONE]")
                            continue;
                        try
                        {
                            auto j = nlohmann::json::parse(payload);
                            if (!j.contains("choices") || j["choices"].empty())
                                continue;
                            auto &delta = j["choices"][0]["delta"];

                            if (delta.contains("reasoning_content") && on_thinking)
                            {
                                std::string rc = delta["reasoning_content"].get<std::string>();
                                if (!rc.empty())
                                    on_thinking(rc);
                            }
                            if (delta.contains("content"))
                            {
                                std::string c = delta["content"].get<std::string>();
                                accumulated += c;
                                if (on_token)
                                    on_token(c);
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
                buffer = buffer.substr(pos);
            };

            CURL *stream_curl = curl_easy_init();
            std::string post_data = request.dump();
            net_unit::CURL_stream_post(stream_curl, url.c_str(), post_data, header, parse_sse);
            curl_easy_cleanup(stream_curl);
            return accumulated;
        }

        std::string models()
        {
            std::string result;
            std::string url = base_url_ + "/api/tags";
            net_unit::CURL_get(curl_easy_init(), url.c_str(), result, "Authorization: Bearer " + api_key_.str());
            return result;
        }
    };
} // namespace LLMProviders

#endif //!__AGENT__H__