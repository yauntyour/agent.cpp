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
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#elif __linux__
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#endif

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

#ifdef _WIN32
                    data += exec("cmd /c chcp 65001>nul && python.exe " +
                                 run_unit::settings["workspace"].get_ref<const std::string &>() +
                                 "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#else
                    data += exec("python3 " +
                                 run_unit::settings["workspace"].get_ref<const std::string &>() +
                                 "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#endif //_WIN32

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
                 return run_unit::tools_list.dump();
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

// 前向声明：频道消息聊天处理器（定义于 app.cpp）。
// bot 线程直接把消息上下文交给进程内 Channel 处理（不再 HTTP 自请求主线程），
// 频道会话与会话列表/WebUI 共用 run_unit::agent_session_manager。
namespace app
{
    nlohmann::json channel_chat(const nlohmann::json &request);
}

namespace bot
{
    using json = nlohmann::json;

    // ════════════════════════════════════════════════════════════════
    // 工具函数
    // ════════════════════════════════════════════════════════════════

    inline std::string str_to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return (char)std::tolower(c); });
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
            // 用原始方法名（POST/PUT），不能小写：CURLOPT_CUSTOMREQUEST 会原样写入
            // 请求行，小写 "post" 会被服务器（如 ilink WAF）拒绝/断开
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
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
    // QR 二维码（SVG → base64）定义见文件末尾（qrcodegen 库之后）
    // ════════════════════════════════════════════════════════════════
    inline std::string qr_svg(const std::string &text);

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
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
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
                        data_resp = app::channel_chat(req);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[" << name << "] channel_chat error: " << e.what() << std::endl;
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
            // 注意：json::array({{"type",1},...}) 会把元素推断成数组 ["type",1]，
            // 必须先用显式对象构造再放入数组，否则服务器返回 "invalid request"
            json item = {{"type", 1}, {"text_item", {{"text", text}}}};
            json payload = {
                {"msg", {{"to_user_id", to_user}, {"message_type", 2}, {"message_state", 2}, {"context_token", ctx_token}, {"client_id", make_client_id()}, {"item_list", json::array({item})}}}};
            json resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, timeout);
            // context_token 可能已过期/一次性使用：带 token 失败时去掉后重试一次，
            // 否则回复会静默丢失（ret != 0 且无错误打印）
            if (resp.value("ret", 0) != 0 && !ctx_token.empty())
            {
                json retry = payload;
                retry["msg"].erase("context_token");
                resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", retry, hdrs, timeout);
            }
            if (resp.value("ret", 0) != 0)
                std::cerr << "[wx] sendmessage failed: " << resp.dump().substr(0, 300) << std::endl;
            return resp;
        }

        inline json send_image(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                               const std::string &to_user, const std::string &ctx_token,
                               const std::string &img_bytes, const std::string &caption = "")
        {
            try
            {
                std::mt19937 rng(std::random_device{}());
                auto rand_key16 = [&rng]()
                {
                    std::string k;
                    k.reserve(16);
                    for (int i = 0; i < 16; ++i)
                        k += (char)(rng() % 256);
                    return k;
                };
                auto key_hex_b64 = [](const std::string &k)
                {
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
                    {"msg", {{"to_user_id", to_user}, {"message_type", 2}, {"message_state", 2}, {"context_token", ctx_token}, {"client_id", make_client_id()}, {"item_list", item_list}}}};
                json resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, 30);
                if (resp.value("ret", 0) != 0 && !ctx_token.empty())
                {
                    json retry = payload;
                    retry["msg"].erase("context_token");
                    resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", retry, hdrs, 30);
                }
                if (resp.value("ret", 0) != 0)
                    std::cerr << "[wx] sendimage failed: " << resp.dump().substr(0, 300) << std::endl;
                return resp;
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
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
        std::string channel_name = cfg.value("channel", "WeChat");

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
                // ilink_call 失败不抛异常（返回 ret!=0）：避免断网时空转打请求
                if (updates.value("ret", 0) != 0)
                {
                    std::string msg = updates.value("msg", updates.value("errmsg", ""));
                    update_status(name, true, "running", "getupdates 失败: " + msg);
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
                        data_resp = app::channel_chat(req);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[" << name << "] channel_chat error: " << e.what() << std::endl;
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
        std::call_once(flag, []()
                       { curl_global_init(CURL_GLOBAL_DEFAULT); });

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

/*
 * QR Code generator library (C++)
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>
/*
 * QR Code generator library (C++)
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace qrcodegen
{

    /*
     * A segment of character/binary/control data in a QR Code symbol.
     * Instances of this class are immutable.
     * The mid-level way to create a segment is to take the payload data
     * and call a static factory function such as QrSegment::makeNumeric().
     * The low-level way to create a segment is to custom-make the bit buffer
     * and call the QrSegment() constructor with appropriate values.
     * This segment class imposes no length restrictions, but QR Codes have restrictions.
     * Even in the most favorable conditions, a QR Code can only hold 7089 characters of data.
     * Any segment longer than this is meaningless for the purpose of generating QR Codes.
     */
    class QrSegment final
    {

        /*---- Public helper enumeration ----*/

        /*
         * Describes how a segment's data bits are interpreted. Immutable.
         */
    public:
        class Mode final
        {

            /*-- Constants --*/

        public:
            static const Mode NUMERIC;

        public:
            static const Mode ALPHANUMERIC;

        public:
            static const Mode BYTE;

        public:
            static const Mode KANJI;

        public:
            static const Mode ECI;

            /*-- Fields --*/

            // The mode indicator bits, which is a uint4 value (range 0 to 15).
        private:
            int modeBits;

            // Number of character count bits for three different version ranges.
        private:
            int numBitsCharCount[3];

            /*-- Constructor --*/

        private:
            Mode(int mode, int cc0, int cc1, int cc2);

            /*-- Methods --*/

            /*
             * (Package-private) Returns the mode indicator bits, which is an unsigned 4-bit value (range 0 to 15).
             */
        public:
            int getModeBits() const;

            /*
             * (Package-private) Returns the bit width of the character count field for a segment in
             * this mode in a QR Code at the given version number. The result is in the range [0, 16].
             */
        public:
            int numCharCountBits(int ver) const;
        };

        /*---- Static factory functions (mid level) ----*/

        /*
         * Returns a segment representing the given binary data encoded in
         * byte mode. All input byte vectors are acceptable. Any text string
         * can be converted to UTF-8 bytes and encoded as a byte mode segment.
         */
    public:
        static QrSegment makeBytes(const std::vector<std::uint8_t> &data);

        /*
         * Returns a segment representing the given string of decimal digits encoded in numeric mode.
         */
    public:
        static QrSegment makeNumeric(const char *digits);

        /*
         * Returns a segment representing the given text string encoded in alphanumeric mode.
         * The characters allowed are: 0 to 9, A to Z (uppercase only), space,
         * dollar, percent, asterisk, plus, hyphen, period, slash, colon.
         */
    public:
        static QrSegment makeAlphanumeric(const char *text);

        /*
         * Returns a list of zero or more segments to represent the given text string. The result
         * may use various segment modes and switch modes to optimize the length of the bit stream.
         */
    public:
        static std::vector<QrSegment> makeSegments(const char *text);

        /*
         * Returns a segment representing an Extended Channel Interpretation
         * (ECI) designator with the given assignment value.
         */
    public:
        static QrSegment makeEci(long assignVal);

        /*---- Public static helper functions ----*/

        /*
         * Tests whether the given string can be encoded as a segment in numeric mode.
         * A string is encodable iff each character is in the range 0 to 9.
         */
    public:
        static bool isNumeric(const char *text);

        /*
         * Tests whether the given string can be encoded as a segment in alphanumeric mode.
         * A string is encodable iff each character is in the following set: 0 to 9, A to Z
         * (uppercase only), space, dollar, percent, asterisk, plus, hyphen, period, slash, colon.
         */
    public:
        static bool isAlphanumeric(const char *text);

        /*---- Instance fields ----*/

        /* The mode indicator of this segment. Accessed through getMode(). */
    private:
        const Mode *mode;

        /* The length of this segment's unencoded data. Measured in characters for
         * numeric/alphanumeric/kanji mode, bytes for byte mode, and 0 for ECI mode.
         * Always zero or positive. Not the same as the data's bit length.
         * Accessed through getNumChars(). */
    private:
        int numChars;

        /* The data bits of this segment. Accessed through getData(). */
    private:
        std::vector<bool> data;

        /*---- Constructors (low level) ----*/

        /*
         * Creates a new QR Code segment with the given attributes and data.
         * The character count (numCh) must agree with the mode and the bit buffer length,
         * but the constraint isn't checked. The given bit buffer is copied and stored.
         */
    public:
        QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt);

        /*
         * Creates a new QR Code segment with the given parameters and data.
         * The character count (numCh) must agree with the mode and the bit buffer length,
         * but the constraint isn't checked. The given bit buffer is moved and stored.
         */
    public:
        QrSegment(const Mode &md, int numCh, std::vector<bool> &&dt);

        /*---- Methods ----*/

        /*
         * Returns the mode field of this segment.
         */
    public:
        const Mode &getMode() const;

        /*
         * Returns the character count field of this segment.
         */
    public:
        int getNumChars() const;

        /*
         * Returns the data bits of this segment.
         */
    public:
        const std::vector<bool> &getData() const;

        // (Package-private) Calculates the number of bits needed to encode the given segments at
        // the given version. Returns a non-negative number if successful. Otherwise returns -1 if a
        // segment has too many characters to fit its length field, or the total bits exceeds INT_MAX.
    public:
        static int getTotalBits(const std::vector<QrSegment> &segs, int version);

        /*---- Private constant ----*/

        /* The set of all legal characters in alphanumeric mode, where
         * each character value maps to the index in the string. */
    private:
        static const char *ALPHANUMERIC_CHARSET;
    };

    /*
     * A QR Code symbol, which is a type of two-dimension barcode.
     * Invented by Denso Wave and described in the ISO/IEC 18004 standard.
     * Instances of this class represent an immutable square grid of dark and light cells.
     * The class provides static factory functions to create a QR Code from text or binary data.
     * The class covers the QR Code Model 2 specification, supporting all versions (sizes)
     * from 1 to 40, all 4 error correction levels, and 4 character encoding modes.
     *
     * Ways to create a QR Code object:
     * - High level: Take the payload data and call QrCode::encodeText() or QrCode::encodeBinary().
     * - Mid level: Custom-make the list of segments and call QrCode::encodeSegments().
     * - Low level: Custom-make the array of data codeword bytes (including
     *   segment headers and final padding, excluding error correction codewords),
     *   supply the appropriate version number, and call the QrCode() constructor.
     * (Note that all ways require supplying the desired error correction level.)
     */
    class QrCode final
    {

        /*---- Public helper enumeration ----*/

        /*
         * The error correction level in a QR Code symbol.
         */
    public:
        enum class Ecc
        {
            LOW = 0,  // The QR Code can tolerate about  7% erroneous codewords
            MEDIUM,   // The QR Code can tolerate about 15% erroneous codewords
            QUARTILE, // The QR Code can tolerate about 25% erroneous codewords
            HIGH,     // The QR Code can tolerate about 30% erroneous codewords
        };

        // Returns a value in the range 0 to 3 (unsigned 2-bit integer).
    private:
        static int getFormatBits(Ecc ecl);

        /*---- Static factory functions (high level) ----*/

        /*
         * Returns a QR Code representing the given Unicode text string at the given error correction level.
         * As a conservative upper bound, this function is guaranteed to succeed for strings that have 2953 or fewer
         * UTF-8 code units (not Unicode code points) if the low error correction level is used. The smallest possible
         * QR Code version is automatically chosen for the output. The ECC level of the result may be higher than
         * the ecl argument if it can be done without increasing the version.
         */
    public:
        static QrCode encodeText(const char *text, Ecc ecl);

        /*
         * Returns a QR Code representing the given binary data at the given error correction level.
         * This function always encodes using the binary segment mode, not any text mode. The maximum number of
         * bytes allowed is 2953. The smallest possible QR Code version is automatically chosen for the output.
         * The ECC level of the result may be higher than the ecl argument if it can be done without increasing the version.
         */
    public:
        static QrCode encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl);

        /*---- Static factory functions (mid level) ----*/

        /*
         * Returns a QR Code representing the given segments with the given encoding parameters.
         * The smallest possible QR Code version within the given range is automatically
         * chosen for the output. Iff boostEcl is true, then the ECC level of the result
         * may be higher than the ecl argument if it can be done without increasing the
         * version. The mask number is either between 0 to 7 (inclusive) to force that
         * mask, or -1 to automatically choose an appropriate mask (which may be slow).
         * This function allows the user to create a custom sequence of segments that switches
         * between modes (such as alphanumeric and byte) to encode text in less space.
         * This is a mid-level API; the high-level API is encodeText() and encodeBinary().
         */
    public:
        static QrCode encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
                                     int minVersion = 1, int maxVersion = 40, int mask = -1, bool boostEcl = true); // All optional parameters

        /*---- Instance fields ----*/

        // Immutable scalar parameters:

        /* The version number of this QR Code, which is between 1 and 40 (inclusive).
         * This determines the size of this barcode. */
    private:
        int version;

        /* The width and height of this QR Code, measured in modules, between
         * 21 and 177 (inclusive). This is equal to version * 4 + 17. */
    private:
        int size;

        /* The error correction level used in this QR Code. */
    private:
        Ecc errorCorrectionLevel;

        /* The index of the mask pattern used in this QR Code, which is between 0 and 7 (inclusive).
         * Even if a QR Code is created with automatic masking requested (mask = -1),
         * the resulting object still has a mask value between 0 and 7. */
    private:
        int mask;

        // Private grids of modules/pixels, with dimensions of size*size:

        // The modules of this QR Code (false = light, true = dark).
        // Immutable after constructor finishes. Accessed through getModule().
    private:
        std::vector<std::vector<bool>> modules;

        // Indicates function modules that are not subjected to masking. Discarded when constructor finishes.
    private:
        std::vector<std::vector<bool>> isFunction;

        /*---- Constructor (low level) ----*/

        /*
         * Creates a new QR Code with the given version number,
         * error correction level, data codeword bytes, and mask number.
         * This is a low-level API that most users should not use directly.
         * A mid-level API is the encodeSegments() function.
         */
    public:
        QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int msk);

        /*---- Public instance methods ----*/

        /*
         * Returns this QR Code's version, in the range [1, 40].
         */
    public:
        int getVersion() const;

        /*
         * Returns this QR Code's size, in the range [21, 177].
         */
    public:
        int getSize() const;

        /*
         * Returns this QR Code's error correction level.
         */
    public:
        Ecc getErrorCorrectionLevel() const;

        /*
         * Returns this QR Code's mask, in the range [0, 7].
         */
    public:
        int getMask() const;

        /*
         * Returns the color of the module (pixel) at the given coordinates, which is false
         * for light or true for dark. The top left corner has the coordinates (x=0, y=0).
         * If the given coordinates are out of bounds, then false (light) is returned.
         */
    public:
        bool getModule(int x, int y) const;

        /*---- Private helper methods for constructor: Drawing function modules ----*/

        // Reads this object's version field, and draws and marks all function modules.
    private:
        void drawFunctionPatterns();

        // Draws two copies of the format bits (with its own error correction code)
        // based on the given mask and this object's error correction level field.
    private:
        void drawFormatBits(int msk);

        // Draws two copies of the version bits (with its own error correction code),
        // based on this object's version field, iff 7 <= version <= 40.
    private:
        void drawVersion();

        // Draws a 9*9 finder pattern including the border separator,
        // with the center module at (x, y). Modules can be out of bounds.
    private:
        void drawFinderPattern(int x, int y);

        // Draws a 5*5 alignment pattern, with the center module
        // at (x, y). All modules must be in bounds.
    private:
        void drawAlignmentPattern(int x, int y);

        // Sets the color of a module and marks it as a function module.
        // Only used by the constructor. Coordinates must be in bounds.
    private:
        void setFunctionModule(int x, int y, bool isDark);

        // Returns the color of the module at the given coordinates, which must be in range.
    private:
        bool module(int x, int y) const;

        /*---- Private helper methods for constructor: Codewords and masking ----*/

        // Returns a new byte string representing the given data with the appropriate error correction
        // codewords appended to it, based on this object's version and error correction level.
    private:
        std::vector<std::uint8_t> addEccAndInterleave(const std::vector<std::uint8_t> &data) const;

        // Draws the given sequence of 8-bit codewords (data and error correction) onto the entire
        // data area of this QR Code. Function modules need to be marked off before this is called.
    private:
        void drawCodewords(const std::vector<std::uint8_t> &data);

        // XORs the codeword modules in this QR Code with the given mask pattern.
        // The function modules must be marked and the codeword bits must be drawn
        // before masking. Due to the arithmetic of XOR, calling applyMask() with
        // the same mask value a second time will undo the mask. A final well-formed
        // QR Code needs exactly one (not zero, two, etc.) mask applied.
    private:
        void applyMask(int msk);

        // Calculates and returns the penalty score based on state of this QR Code's current modules.
        // This is used by the automatic mask choice algorithm to find the mask pattern that yields the lowest score.
    private:
        long getPenaltyScore() const;

        /*---- Private helper functions ----*/

        // Returns an ascending list of positions of alignment patterns for this version number.
        // Each position is in the range [0,177), and are used on both the x and y axes.
        // This could be implemented as lookup table of 40 variable-length lists of unsigned bytes.
    private:
        std::vector<int> getAlignmentPatternPositions() const;

        // Returns the number of data bits that can be stored in a QR Code of the given version number, after
        // all function modules are excluded. This includes remainder bits, so it might not be a multiple of 8.
        // The result is in the range [208, 29648]. This could be implemented as a 40-entry lookup table.
    private:
        static int getNumRawDataModules(int ver);

        // Returns the number of 8-bit data (i.e. not error correction) codewords contained in any
        // QR Code of the given version number and error correction level, with remainder bits discarded.
        // This stateless pure function could be implemented as a (40*4)-cell lookup table.
    private:
        static int getNumDataCodewords(int ver, Ecc ecl);

        // Returns a Reed-Solomon ECC generator polynomial for the given degree. This could be
        // implemented as a lookup table over all possible parameter values, instead of as an algorithm.
    private:
        static std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree);

        // Returns the Reed-Solomon error correction codeword for the given data and divisor polynomials.
    private:
        static std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor);

        // Returns the product of the two given field elements modulo GF(2^8/0x11D).
        // All inputs are valid. This could be implemented as a 256*256 lookup table.
    private:
        static std::uint8_t reedSolomonMultiply(std::uint8_t x, std::uint8_t y);

        // Can only be called immediately after a light run is added, and
        // returns either 0, 1, or 2. A helper function for getPenaltyScore().
    private:
        int finderPenaltyCountPatterns(const std::array<int, 7> &runHistory) const;

        // Must be called at the end of a line (row or column) of modules. A helper function for getPenaltyScore().
    private:
        int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory) const;

        // Pushes the given value to the front and drops the last value. A helper function for getPenaltyScore().
    private:
        void finderPenaltyAddHistory(int currentRunLength, std::array<int, 7> &runHistory) const;

        // Returns true iff the i'th bit of x is set to 1.
    private:
        static bool getBit(long x, int i);

        /*---- Constants and tables ----*/

        // The minimum version number supported in the QR Code Model 2 standard.
    public:
        static constexpr int MIN_VERSION = 1;

        // The maximum version number supported in the QR Code Model 2 standard.
    public:
        static constexpr int MAX_VERSION = 40;

        // For use in getPenaltyScore(), when evaluating which mask is best.
    private:
        static const int PENALTY_N1;

    private:
        static const int PENALTY_N2;

    private:
        static const int PENALTY_N3;

    private:
        static const int PENALTY_N4;

    private:
        static const std::int8_t ECC_CODEWORDS_PER_BLOCK[4][41];

    private:
        static const std::int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41];
    };

    /*---- Public exception class ----*/

    /*
     * Thrown when the supplied data does not fit any QR Code version. Ways to handle this exception include:
     * - Decrease the error correction level if it was greater than Ecc::LOW.
     * - If the encodeSegments() function was called with a maxVersion argument, then increase
     *   it if it was less than QrCode::MAX_VERSION. (This advice does not apply to the other
     *   factory functions because they search all versions up to QrCode::MAX_VERSION.)
     * - Split the text data into better or optimal segments in order to reduce the number of bits required.
     * - Change the text or binary data to be shorter.
     * - Change the text to fit the character set of a particular segment mode (e.g. alphanumeric).
     * - Propagate the error upward to the caller/user.
     */
    class data_too_long : public std::length_error
    {

    public:
        explicit data_too_long(const std::string &msg);
    };

    /*
     * An appendable sequence of bits (0s and 1s). Mainly used by QrSegment.
     */
    class BitBuffer final : public std::vector<bool>
    {

        /*---- Constructor ----*/

        // Creates an empty bit buffer (length 0).
    public:
        BitBuffer();

        /*---- Method ----*/

        // Appends the given number of low-order bits of the given value
        // to this buffer. Requires 0 <= len <= 31 and val < 2^len.
    public:
        void appendBits(std::uint32_t val, int len);
    };

}

using std::int8_t;
using std::size_t;
using std::uint8_t;
using std::vector;

namespace qrcodegen
{

    /*---- Class QrSegment ----*/

    QrSegment::Mode::Mode(int mode, int cc0, int cc1, int cc2) : modeBits(mode)
    {
        numBitsCharCount[0] = cc0;
        numBitsCharCount[1] = cc1;
        numBitsCharCount[2] = cc2;
    }

    int QrSegment::Mode::getModeBits() const
    {
        return modeBits;
    }

    int QrSegment::Mode::numCharCountBits(int ver) const
    {
        return numBitsCharCount[(ver + 7) / 17];
    }

    const QrSegment::Mode QrSegment::Mode::NUMERIC(0x1, 10, 12, 14);
    const QrSegment::Mode QrSegment::Mode::ALPHANUMERIC(0x2, 9, 11, 13);
    const QrSegment::Mode QrSegment::Mode::BYTE(0x4, 8, 16, 16);
    const QrSegment::Mode QrSegment::Mode::KANJI(0x8, 8, 10, 12);
    const QrSegment::Mode QrSegment::Mode::ECI(0x7, 0, 0, 0);

    QrSegment QrSegment::makeBytes(const vector<uint8_t> &data)
    {
        if (data.size() > static_cast<unsigned int>(INT_MAX))
            throw std::length_error("Data too long");
        BitBuffer bb;
        for (uint8_t b : data)
            bb.appendBits(b, 8);
        return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
    }

    QrSegment QrSegment::makeNumeric(const char *digits)
    {
        BitBuffer bb;
        int accumData = 0;
        int accumCount = 0;
        int charCount = 0;
        for (; *digits != '\0'; digits++, charCount++)
        {
            char c = *digits;
            if (c < '0' || c > '9')
                throw std::domain_error("String contains non-numeric characters");
            accumData = accumData * 10 + (c - '0');
            accumCount++;
            if (accumCount == 3)
            {
                bb.appendBits(static_cast<uint32_t>(accumData), 10);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) // 1 or 2 digits remaining
            bb.appendBits(static_cast<uint32_t>(accumData), accumCount * 3 + 1);
        return QrSegment(Mode::NUMERIC, charCount, std::move(bb));
    }

    QrSegment QrSegment::makeAlphanumeric(const char *text)
    {
        BitBuffer bb;
        int accumData = 0;
        int accumCount = 0;
        int charCount = 0;
        for (; *text != '\0'; text++, charCount++)
        {
            const char *temp = std::strchr(ALPHANUMERIC_CHARSET, *text);
            if (temp == nullptr)
                throw std::domain_error("String contains unencodable characters in alphanumeric mode");
            accumData = accumData * 45 + static_cast<int>(temp - ALPHANUMERIC_CHARSET);
            accumCount++;
            if (accumCount == 2)
            {
                bb.appendBits(static_cast<uint32_t>(accumData), 11);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) // 1 character remaining
            bb.appendBits(static_cast<uint32_t>(accumData), 6);
        return QrSegment(Mode::ALPHANUMERIC, charCount, std::move(bb));
    }

    vector<QrSegment> QrSegment::makeSegments(const char *text)
    {
        // Select the most efficient segment encoding automatically
        vector<QrSegment> result;
        if (*text == '\0')
            ; // Leave result empty
        else if (isNumeric(text))
            result.push_back(makeNumeric(text));
        else if (isAlphanumeric(text))
            result.push_back(makeAlphanumeric(text));
        else
        {
            vector<uint8_t> bytes;
            for (; *text != '\0'; text++)
                bytes.push_back(static_cast<uint8_t>(*text));
            result.push_back(makeBytes(bytes));
        }
        return result;
    }

    QrSegment QrSegment::makeEci(long assignVal)
    {
        BitBuffer bb;
        if (assignVal < 0)
            throw std::domain_error("ECI assignment value out of range");
        else if (assignVal < (1 << 7))
            bb.appendBits(static_cast<uint32_t>(assignVal), 8);
        else if (assignVal < (1 << 14))
        {
            bb.appendBits(2, 2);
            bb.appendBits(static_cast<uint32_t>(assignVal), 14);
        }
        else if (assignVal < 1000000L)
        {
            bb.appendBits(6, 3);
            bb.appendBits(static_cast<uint32_t>(assignVal), 21);
        }
        else
            throw std::domain_error("ECI assignment value out of range");
        return QrSegment(Mode::ECI, 0, std::move(bb));
    }

    QrSegment::QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt) : mode(&md),
                                                                                   numChars(numCh),
                                                                                   data(dt)
    {
        if (numCh < 0)
            throw std::domain_error("Invalid value");
    }

    QrSegment::QrSegment(const Mode &md, int numCh, std::vector<bool> &&dt) : mode(&md),
                                                                              numChars(numCh),
                                                                              data(std::move(dt))
    {
        if (numCh < 0)
            throw std::domain_error("Invalid value");
    }

    int QrSegment::getTotalBits(const vector<QrSegment> &segs, int version)
    {
        int result = 0;
        for (const QrSegment &seg : segs)
        {
            int ccbits = seg.mode->numCharCountBits(version);
            if (seg.numChars >= (1L << ccbits))
                return -1; // The segment's length doesn't fit the field's bit width
            if (4 + ccbits > INT_MAX - result)
                return -1; // The sum will overflow an int type
            result += 4 + ccbits;
            if (seg.data.size() > static_cast<unsigned int>(INT_MAX - result))
                return -1; // The sum will overflow an int type
            result += static_cast<int>(seg.data.size());
        }
        return result;
    }

    bool QrSegment::isNumeric(const char *text)
    {
        for (; *text != '\0'; text++)
        {
            char c = *text;
            if (c < '0' || c > '9')
                return false;
        }
        return true;
    }

    bool QrSegment::isAlphanumeric(const char *text)
    {
        for (; *text != '\0'; text++)
        {
            if (std::strchr(ALPHANUMERIC_CHARSET, *text) == nullptr)
                return false;
        }
        return true;
    }

    const QrSegment::Mode &QrSegment::getMode() const
    {
        return *mode;
    }

    int QrSegment::getNumChars() const
    {
        return numChars;
    }

    const std::vector<bool> &QrSegment::getData() const
    {
        return data;
    }

    const char *QrSegment::ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

    /*---- Class QrCode ----*/

    int QrCode::getFormatBits(Ecc ecl)
    {
        switch (ecl)
        {
        case Ecc::LOW:
            return 1;
        case Ecc::MEDIUM:
            return 0;
        case Ecc::QUARTILE:
            return 3;
        case Ecc::HIGH:
            return 2;
        default:
            throw std::logic_error("Unreachable");
        }
    }

    QrCode QrCode::encodeText(const char *text, Ecc ecl)
    {
        vector<QrSegment> segs = QrSegment::makeSegments(text);
        return encodeSegments(segs, ecl);
    }

    QrCode QrCode::encodeBinary(const vector<uint8_t> &data, Ecc ecl)
    {
        vector<QrSegment> segs{QrSegment::makeBytes(data)};
        return encodeSegments(segs, ecl);
    }

    QrCode QrCode::encodeSegments(const vector<QrSegment> &segs, Ecc ecl,
                                  int minVersion, int maxVersion, int mask, bool boostEcl)
    {
        if (!(MIN_VERSION <= minVersion && minVersion <= maxVersion && maxVersion <= MAX_VERSION) || mask < -1 || mask > 7)
            throw std::invalid_argument("Invalid value");

        // Find the minimal version number to use
        int version, dataUsedBits;
        for (version = minVersion;; version++)
        {
            int dataCapacityBits = getNumDataCodewords(version, ecl) * 8; // Number of data bits available
            dataUsedBits = QrSegment::getTotalBits(segs, version);
            if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
                break; // This version number is found to be suitable
            if (version >= maxVersion)
            { // All versions in the range could not fit the given data
                std::ostringstream sb;
                if (dataUsedBits == -1)
                    sb << "Segment too long";
                else
                {
                    sb << "Data length = " << dataUsedBits << " bits, ";
                    sb << "Max capacity = " << dataCapacityBits << " bits";
                }
                throw data_too_long(sb.str());
            }
        }
        assert(dataUsedBits != -1);

        // Increase the error correction level while the data still fits in the current version number
        for (Ecc newEcl : {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH})
        { // From low to high
            if (boostEcl && dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
                ecl = newEcl;
        }

        // Concatenate all segments to create the data bit string
        BitBuffer bb;
        for (const QrSegment &seg : segs)
        {
            bb.appendBits(static_cast<uint32_t>(seg.getMode().getModeBits()), 4);
            bb.appendBits(static_cast<uint32_t>(seg.getNumChars()), seg.getMode().numCharCountBits(version));
            bb.insert(bb.end(), seg.getData().begin(), seg.getData().end());
        }
        assert(bb.size() == static_cast<unsigned int>(dataUsedBits));

        // Add terminator and pad up to a byte if applicable
        size_t dataCapacityBits = static_cast<size_t>(getNumDataCodewords(version, ecl)) * 8;
        assert(bb.size() <= dataCapacityBits);
        bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
        bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
        assert(bb.size() % 8 == 0);

        // Pad with alternating bytes until data capacity is reached
        for (uint8_t padByte = 0xEC; bb.size() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
            bb.appendBits(padByte, 8);

        // Pack bits into bytes in big endian
        vector<uint8_t> dataCodewords(bb.size() / 8);
        for (size_t i = 0; i < bb.size(); i++)
            dataCodewords.at(i >> 3) |= (bb.at(i) ? 1 : 0) << (7 - (i & 7));

        // Create the QR Code object
        return QrCode(version, ecl, dataCodewords, mask);
    }

    QrCode::QrCode(int ver, Ecc ecl, const vector<uint8_t> &dataCodewords, int msk) : // Initialize fields and check arguments
                                                                                      version(ver),
                                                                                      errorCorrectionLevel(ecl)
    {
        if (ver < MIN_VERSION || ver > MAX_VERSION)
            throw std::domain_error("Version value out of range");
        if (msk < -1 || msk > 7)
            throw std::domain_error("Mask value out of range");
        size = ver * 4 + 17;
        size_t sz = static_cast<size_t>(size);
        modules = vector<vector<bool>>(sz, vector<bool>(sz)); // Initially all light
        isFunction = vector<vector<bool>>(sz, vector<bool>(sz));

        // Compute ECC, draw modules
        drawFunctionPatterns();
        const vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
        drawCodewords(allCodewords);

        // Do masking
        if (msk == -1)
        { // Automatically choose best mask
            long minPenalty = LONG_MAX;
            for (int i = 0; i < 8; i++)
            {
                applyMask(i);
                drawFormatBits(i);
                long penalty = getPenaltyScore();
                if (penalty < minPenalty)
                {
                    msk = i;
                    minPenalty = penalty;
                }
                applyMask(i); // Undoes the mask due to XOR
            }
        }
        assert(0 <= msk && msk <= 7);
        mask = msk;
        applyMask(msk);      // Apply the final choice of mask
        drawFormatBits(msk); // Overwrite old format bits

        isFunction.clear();
        isFunction.shrink_to_fit();
    }

    int QrCode::getVersion() const
    {
        return version;
    }

    int QrCode::getSize() const
    {
        return size;
    }

    QrCode::Ecc QrCode::getErrorCorrectionLevel() const
    {
        return errorCorrectionLevel;
    }

    int QrCode::getMask() const
    {
        return mask;
    }

    bool QrCode::getModule(int x, int y) const
    {
        return 0 <= x && x < size && 0 <= y && y < size && module(x, y);
    }

    void QrCode::drawFunctionPatterns()
    {
        // Draw horizontal and vertical timing patterns
        for (int i = 0; i < size; i++)
        {
            setFunctionModule(6, i, i % 2 == 0);
            setFunctionModule(i, 6, i % 2 == 0);
        }

        // Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
        drawFinderPattern(3, 3);
        drawFinderPattern(size - 4, 3);
        drawFinderPattern(3, size - 4);

        // Draw numerous alignment patterns
        const vector<int> alignPatPos = getAlignmentPatternPositions();
        size_t numAlign = alignPatPos.size();
        for (size_t i = 0; i < numAlign; i++)
        {
            for (size_t j = 0; j < numAlign; j++)
            {
                // Don't draw on the three finder corners
                if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
                    drawAlignmentPattern(alignPatPos.at(i), alignPatPos.at(j));
            }
        }

        // Draw configuration data
        drawFormatBits(0); // Dummy mask value; overwritten later in the constructor
        drawVersion();
    }

    void QrCode::drawFormatBits(int msk)
    {
        // Calculate error correction code and pack bits
        int data = getFormatBits(errorCorrectionLevel) << 3 | msk; // errCorrLvl is uint2, msk is uint3
        int rem = data;
        for (int i = 0; i < 10; i++)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412; // uint15
        assert(bits >> 15 == 0);

        // Draw first copy
        for (int i = 0; i <= 5; i++)
            setFunctionModule(8, i, getBit(bits, i));
        setFunctionModule(8, 7, getBit(bits, 6));
        setFunctionModule(8, 8, getBit(bits, 7));
        setFunctionModule(7, 8, getBit(bits, 8));
        for (int i = 9; i < 15; i++)
            setFunctionModule(14 - i, 8, getBit(bits, i));

        // Draw second copy
        for (int i = 0; i < 8; i++)
            setFunctionModule(size - 1 - i, 8, getBit(bits, i));
        for (int i = 8; i < 15; i++)
            setFunctionModule(8, size - 15 + i, getBit(bits, i));
        setFunctionModule(8, size - 8, true); // Always dark
    }

    void QrCode::drawVersion()
    {
        if (version < 7)
            return;

        // Calculate error correction code and pack bits
        int rem = version; // version is uint6, in the range [7, 40]
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = static_cast<long>(version) << 12 | rem; // uint18
        assert(bits >> 18 == 0);

        // Draw two copies
        for (int i = 0; i < 18; i++)
        {
            bool bit = getBit(bits, i);
            int a = size - 11 + i % 3;
            int b = i / 3;
            setFunctionModule(a, b, bit);
            setFunctionModule(b, a, bit);
        }
    }

    void QrCode::drawFinderPattern(int x, int y)
    {
        for (int dy = -4; dy <= 4; dy++)
        {
            for (int dx = -4; dx <= 4; dx++)
            {
                int dist = std::max(std::abs(dx), std::abs(dy)); // Chebyshev/infinity norm
                int xx = x + dx, yy = y + dy;
                if (0 <= xx && xx < size && 0 <= yy && yy < size)
                    setFunctionModule(xx, yy, dist != 2 && dist != 4);
            }
        }
    }

    void QrCode::drawAlignmentPattern(int x, int y)
    {
        for (int dy = -2; dy <= 2; dy++)
        {
            for (int dx = -2; dx <= 2; dx++)
                setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }

    void QrCode::setFunctionModule(int x, int y, bool isDark)
    {
        size_t ux = static_cast<size_t>(x);
        size_t uy = static_cast<size_t>(y);
        modules.at(uy).at(ux) = isDark;
        isFunction.at(uy).at(ux) = true;
    }

    bool QrCode::module(int x, int y) const
    {
        return modules.at(static_cast<size_t>(y)).at(static_cast<size_t>(x));
    }

    vector<uint8_t> QrCode::addEccAndInterleave(const vector<uint8_t> &data) const
    {
        if (data.size() != static_cast<unsigned int>(getNumDataCodewords(version, errorCorrectionLevel)))
            throw std::invalid_argument("Invalid argument");

        // Calculate parameter numbers
        int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(errorCorrectionLevel)][version];
        int blockEccLen = ECC_CODEWORDS_PER_BLOCK[static_cast<int>(errorCorrectionLevel)][version];
        int rawCodewords = getNumRawDataModules(version) / 8;
        int numShortBlocks = numBlocks - rawCodewords % numBlocks;
        int shortBlockLen = rawCodewords / numBlocks;

        // Split data into blocks and append ECC to each block
        vector<vector<uint8_t>> blocks;
        const vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
        for (int i = 0, k = 0; i < numBlocks; i++)
        {
            vector<uint8_t> dat(data.cbegin() + k, data.cbegin() + (k + shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1)));
            k += static_cast<int>(dat.size());
            const vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
            if (i < numShortBlocks)
                dat.push_back(0);
            dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
            blocks.push_back(std::move(dat));
        }

        // Interleave (not concatenate) the bytes from every block into a single sequence
        vector<uint8_t> result;
        for (size_t i = 0; i < blocks.at(0).size(); i++)
        {
            for (size_t j = 0; j < blocks.size(); j++)
            {
                // Skip the padding byte in short blocks
                if (i != static_cast<unsigned int>(shortBlockLen - blockEccLen) || j >= static_cast<unsigned int>(numShortBlocks))
                    result.push_back(blocks.at(j).at(i));
            }
        }
        assert(result.size() == static_cast<unsigned int>(rawCodewords));
        return result;
    }

    void QrCode::drawCodewords(const vector<uint8_t> &data)
    {
        if (data.size() != static_cast<unsigned int>(getNumRawDataModules(version) / 8))
            throw std::invalid_argument("Invalid argument");

        size_t i = 0; // Bit index into the data
        // Do the funny zigzag scan
        for (int right = size - 1; right >= 1; right -= 2)
        { // Index of right column in each column pair
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < size; vert++)
            { // Vertical counter
                for (int j = 0; j < 2; j++)
                {
                    size_t x = static_cast<size_t>(right - j); // Actual x coordinate
                    bool upward = ((right + 1) & 2) == 0;
                    size_t y = static_cast<size_t>(upward ? size - 1 - vert : vert); // Actual y coordinate
                    if (!isFunction.at(y).at(x) && i < data.size() * 8)
                    {
                        modules.at(y).at(x) = getBit(data.at(i >> 3), 7 - static_cast<int>(i & 7));
                        i++;
                    }
                    // If this QR Code has any remainder bits (0 to 7), they were assigned as
                    // 0/false/light by the constructor and are left unchanged by this method
                }
            }
        }
        assert(i == data.size() * 8);
    }

    void QrCode::applyMask(int msk)
    {
        if (msk < 0 || msk > 7)
            throw std::domain_error("Mask value out of range");
        size_t sz = static_cast<size_t>(size);
        for (size_t y = 0; y < sz; y++)
        {
            for (size_t x = 0; x < sz; x++)
            {
                bool invert;
                switch (msk)
                {
                case 0:
                    invert = (x + y) % 2 == 0;
                    break;
                case 1:
                    invert = y % 2 == 0;
                    break;
                case 2:
                    invert = x % 3 == 0;
                    break;
                case 3:
                    invert = (x + y) % 3 == 0;
                    break;
                case 4:
                    invert = (x / 3 + y / 2) % 2 == 0;
                    break;
                case 5:
                    invert = x * y % 2 + x * y % 3 == 0;
                    break;
                case 6:
                    invert = (x * y % 2 + x * y % 3) % 2 == 0;
                    break;
                case 7:
                    invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
                    break;
                default:
                    throw std::logic_error("Unreachable");
                }
                modules.at(y).at(x) = modules.at(y).at(x) ^ (invert & !isFunction.at(y).at(x));
            }
        }
    }

    long QrCode::getPenaltyScore() const
    {
        long result = 0;

        // Adjacent modules in row having same color, and finder-like patterns
        for (int y = 0; y < size; y++)
        {
            bool runColor = false;
            int runX = 0;
            std::array<int, 7> runHistory = {};
            for (int x = 0; x < size; x++)
            {
                if (module(x, y) == runColor)
                {
                    runX++;
                    if (runX == 5)
                        result += PENALTY_N1;
                    else if (runX > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runX, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
                    runColor = module(x, y);
                    runX = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runX, runHistory) * PENALTY_N3;
        }
        // Adjacent modules in column having same color, and finder-like patterns
        for (int x = 0; x < size; x++)
        {
            bool runColor = false;
            int runY = 0;
            std::array<int, 7> runHistory = {};
            for (int y = 0; y < size; y++)
            {
                if (module(x, y) == runColor)
                {
                    runY++;
                    if (runY == 5)
                        result += PENALTY_N1;
                    else if (runY > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runY, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
                    runColor = module(x, y);
                    runY = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runY, runHistory) * PENALTY_N3;
        }

        // 2*2 blocks of modules having same color
        for (int y = 0; y < size - 1; y++)
        {
            for (int x = 0; x < size - 1; x++)
            {
                bool color = module(x, y);
                if (color == module(x + 1, y) &&
                    color == module(x, y + 1) &&
                    color == module(x + 1, y + 1))
                    result += PENALTY_N2;
            }
        }

        // Balance of dark and light modules
        int dark = 0;
        for (const vector<bool> &row : modules)
        {
            for (bool color : row)
            {
                if (color)
                    dark++;
            }
        }
        int total = size * size; // Note that size is odd, so dark/total != 1/2
        // Compute the smallest integer k >= 0 such that (45-5k)% <= dark/total <= (55+5k)%
        int k = static_cast<int>((std::abs(dark * 20L - total * 10L) + total - 1) / total) - 1;
        assert(0 <= k && k <= 9);
        result += k * PENALTY_N4;
        assert(0 <= result && result <= 2568888L); // Non-tight upper bound based on default values of PENALTY_N1, ..., N4
        return result;
    }

    vector<int> QrCode::getAlignmentPatternPositions() const
    {
        if (version == 1)
            return vector<int>();
        else
        {
            int numAlign = version / 7 + 2;
            int step = (version * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4) * 2;
            vector<int> result;
            for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
                result.insert(result.begin(), pos);
            result.insert(result.begin(), 6);
            return result;
        }
    }

    int QrCode::getNumRawDataModules(int ver)
    {
        if (ver < MIN_VERSION || ver > MAX_VERSION)
            throw std::domain_error("Version number out of range");
        int result = (16 * ver + 128) * ver + 64;
        if (ver >= 2)
        {
            int numAlign = ver / 7 + 2;
            result -= (25 * numAlign - 10) * numAlign - 55;
            if (ver >= 7)
                result -= 36;
        }
        assert(208 <= result && result <= 29648);
        return result;
    }

    int QrCode::getNumDataCodewords(int ver, Ecc ecl)
    {
        return getNumRawDataModules(ver) / 8 - ECC_CODEWORDS_PER_BLOCK[static_cast<int>(ecl)][ver] * NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(ecl)][ver];
    }

    vector<uint8_t> QrCode::reedSolomonComputeDivisor(int degree)
    {
        if (degree < 1 || degree > 255)
            throw std::domain_error("Degree out of range");
        // Polynomial coefficients are stored from highest to lowest power, excluding the leading term which is always 1.
        // For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
        vector<uint8_t> result(static_cast<size_t>(degree));
        result.at(result.size() - 1) = 1; // Start off with the monomial x^0

        // Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
        // and drop the highest monomial term which is always 1x^degree.
        // Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
        uint8_t root = 1;
        for (int i = 0; i < degree; i++)
        {
            // Multiply the current product by (x - r^i)
            for (size_t j = 0; j < result.size(); j++)
            {
                result.at(j) = reedSolomonMultiply(result.at(j), root);
                if (j + 1 < result.size())
                    result.at(j) ^= result.at(j + 1);
            }
            root = reedSolomonMultiply(root, 0x02);
        }
        return result;
    }

    vector<uint8_t> QrCode::reedSolomonComputeRemainder(const vector<uint8_t> &data, const vector<uint8_t> &divisor)
    {
        vector<uint8_t> result(divisor.size());
        for (uint8_t b : data)
        { // Polynomial division
            uint8_t factor = b ^ result.at(0);
            result.erase(result.begin());
            result.push_back(0);
            for (size_t i = 0; i < result.size(); i++)
                result.at(i) ^= reedSolomonMultiply(divisor.at(i), factor);
        }
        return result;
    }

    uint8_t QrCode::reedSolomonMultiply(uint8_t x, uint8_t y)
    {
        // Russian peasant multiplication
        int z = 0;
        for (int i = 7; i >= 0; i--)
        {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        assert(z >> 8 == 0);
        return static_cast<uint8_t>(z);
    }

    int QrCode::finderPenaltyCountPatterns(const std::array<int, 7> &runHistory) const
    {
        int n = runHistory.at(1);
        assert(n <= size * 3);
        bool core = n > 0 && runHistory.at(2) == n && runHistory.at(3) == n * 3 && runHistory.at(4) == n && runHistory.at(5) == n;
        return (core && runHistory.at(0) >= n * 4 && runHistory.at(6) >= n ? 1 : 0) + (core && runHistory.at(6) >= n * 4 && runHistory.at(0) >= n ? 1 : 0);
    }

    int QrCode::finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory) const
    {
        if (currentRunColor)
        { // Terminate dark run
            finderPenaltyAddHistory(currentRunLength, runHistory);
            currentRunLength = 0;
        }
        currentRunLength += size; // Add light border to final run
        finderPenaltyAddHistory(currentRunLength, runHistory);
        return finderPenaltyCountPatterns(runHistory);
    }

    void QrCode::finderPenaltyAddHistory(int currentRunLength, std::array<int, 7> &runHistory) const
    {
        if (runHistory.at(0) == 0)
            currentRunLength += size; // Add light border to initial run
        std::copy_backward(runHistory.cbegin(), runHistory.cend() - 1, runHistory.end());
        runHistory.at(0) = currentRunLength;
    }

    bool QrCode::getBit(long x, int i)
    {
        return ((x >> i) & 1) != 0;
    }

    /*---- Tables of constants ----*/

    const int QrCode::PENALTY_N1 = 3;
    const int QrCode::PENALTY_N2 = 3;
    const int QrCode::PENALTY_N3 = 40;
    const int QrCode::PENALTY_N4 = 10;

    const int8_t QrCode::ECC_CODEWORDS_PER_BLOCK[4][41] = {
        // Version: (note that index 0 is for padding, and is set to an illegal value)
        // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
        {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Low
        {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, // Medium
        {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Quartile
        {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // High
    };

    const int8_t QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
        // Version: (note that index 0 is for padding, and is set to an illegal value)
        // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
        {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},              // Low
        {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},     // Medium
        {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
        {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81}, // High
    };

    data_too_long::data_too_long(const std::string &msg) : std::length_error(msg) {}

    /*---- Class BitBuffer ----*/

    BitBuffer::BitBuffer()
        : std::vector<bool>() {}

    void BitBuffer::appendBits(std::uint32_t val, int len)
    {
        if (len < 0 || len > 31 || val >> len != 0)
            throw std::domain_error("Value out of range");
        for (int i = len - 1; i >= 0; i--) // Append bit by bit
            this->push_back(((val >> i) & 1) != 0);
    }

}

    // ════════════════════════════════════════════════════════════════
    // QR 二维码（SVG → base64）
    // ════════════════════════════════════════════════════════════════
    namespace bot
    {
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
    } // namespace bot

#endif //!__AGENT__H__