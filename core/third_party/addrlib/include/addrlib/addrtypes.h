/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/**
***************************************************************************************************
* @file  addrtypes.h
* @brief Contains the helper function and constants
***************************************************************************************************
*/

#pragma once
#include <cstdint>

#if !defined(TILEINDEX_INVALID)
#define TILEINDEX_INVALID                -1
#endif

#if !defined(TILEINDEX_LINEAR_GENERAL)
#define TILEINDEX_LINEAR_GENERAL         -2
#endif

#if !defined(TILEINDEX_LINEAR_ALIGNED)
#define TILEINDEX_LINEAR_ALIGNED          8
#endif


/**
***************************************************************************************************
* Return codes
***************************************************************************************************
*/
enum ADDR_E_RETURNCODE : uint32_t
{
   ADDR_OK = 0x0,
   ADDR_ERROR = 0x1,
   ADDR_OUTOFMEMORY = 0x2,
   ADDR_INVALIDPARAMS = 0x3,
   ADDR_NOTSUPPORTED = 0x4,
   ADDR_NOTIMPLEMENTED = 0x5,
   ADDR_PARAMSIZEMISMATCH = 0x6,
};

enum AddrChipEngine : uint32_t
{
   CIASICIDGFXENGINE_R600 = 6,
};



/**
***************************************************************************************************
* @brief
*   Neutral enums that define tile modes for all H/W
* @note
*   R600/R800 tiling mode can be cast to hw enums directly but never cast into HW enum from
*   ADDR_TM_2D_TILED_XTHICK
*
***************************************************************************************************
*/
enum AddrTileMode : uint32_t
{
   ADDR_TM_LINEAR_GENERAL = 0x0,
   ADDR_TM_LINEAR_ALIGNED = 0x1,
   ADDR_TM_1D_TILED_THIN1 = 0x2,
   ADDR_TM_1D_TILED_THICK = 0x3,
   ADDR_TM_2D_TILED_THIN1 = 0x4,
   ADDR_TM_2D_TILED_THIN2 = 0x5,
   ADDR_TM_2D_TILED_THIN4 = 0x6,
   ADDR_TM_2D_TILED_THICK = 0x7,
   ADDR_TM_2B_TILED_THIN1 = 0x8,
   ADDR_TM_2B_TILED_THIN2 = 0x9,
   ADDR_TM_2B_TILED_THIN4 = 0xA,
   ADDR_TM_2B_TILED_THICK = 0xB,
   ADDR_TM_3D_TILED_THIN1 = 0xC,
   ADDR_TM_3D_TILED_THICK = 0xD,
   ADDR_TM_3B_TILED_THIN1 = 0xE,
   ADDR_TM_3B_TILED_THICK = 0xF,
   ADDR_TM_LINEAR_SPECIAL = 0x10,
   ADDR_TM_2D_TILED_XTHICK = 0x10,
   ADDR_TM_3D_TILED_XTHICK = 0x11,
   ADDR_TM_POWER_SAVE = 0x12,
   ADDR_TM_COUNT = 0x13,
};


/**
***************************************************************************************************
*   AddrFormat
*
*   @brief
*       Neutral enum for SurfaceFormat
*
***************************************************************************************************
*/
enum AddrFormat : uint32_t
{
   ADDR_FMT_INVALID = 0x0,
   ADDR_FMT_8 = 0x1,
   ADDR_FMT_4_4 = 0x2,
   ADDR_FMT_3_3_2 = 0x3,
   ADDR_FMT_RESERVED_4 = 0x4,
   ADDR_FMT_16 = 0x5,
   ADDR_FMT_16_FLOAT = 0x6,
   ADDR_FMT_8_8 = 0x7,
   ADDR_FMT_5_6_5 = 0x8,
   ADDR_FMT_6_5_5 = 0x9,
   ADDR_FMT_1_5_5_5 = 0xA,
   ADDR_FMT_4_4_4_4 = 0xB,
   ADDR_FMT_5_5_5_1 = 0xC,
   ADDR_FMT_32 = 0xD,
   ADDR_FMT_32_FLOAT = 0xE,
   ADDR_FMT_16_16 = 0xF,
   ADDR_FMT_16_16_FLOAT = 0x10,
   ADDR_FMT_8_24 = 0x11,
   ADDR_FMT_8_24_FLOAT = 0x12,
   ADDR_FMT_24_8 = 0x13,
   ADDR_FMT_24_8_FLOAT = 0x14,
   ADDR_FMT_10_11_11 = 0x15,
   ADDR_FMT_10_11_11_FLOAT = 0x16,
   ADDR_FMT_11_11_10 = 0x17,
   ADDR_FMT_11_11_10_FLOAT = 0x18,
   ADDR_FMT_2_10_10_10 = 0x19,
   ADDR_FMT_8_8_8_8 = 0x1A,
   ADDR_FMT_10_10_10_2 = 0x1B,
   ADDR_FMT_X24_8_32_FLOAT = 0x1C,
   ADDR_FMT_32_32 = 0x1D,
   ADDR_FMT_32_32_FLOAT = 0x1E,
   ADDR_FMT_16_16_16_16 = 0x1F,
   ADDR_FMT_16_16_16_16_FLOAT = 0x20,
   ADDR_FMT_RESERVED_33 = 0x21,
   ADDR_FMT_32_32_32_32 = 0x22,
   ADDR_FMT_32_32_32_32_FLOAT = 0x23,
   ADDR_FMT_RESERVED_36 = 0x24,
   ADDR_FMT_1 = 0x25,
   ADDR_FMT_1_REVERSED = 0x26,
   ADDR_FMT_GB_GR = 0x27,
   ADDR_FMT_BG_RG = 0x28,
   ADDR_FMT_32_AS_8 = 0x29,
   ADDR_FMT_32_AS_8_8 = 0x2A,
   ADDR_FMT_5_9_9_9_SHAREDEXP = 0x2B,
   ADDR_FMT_8_8_8 = 0x2C,
   ADDR_FMT_16_16_16 = 0x2D,
   ADDR_FMT_16_16_16_FLOAT = 0x2E,
   ADDR_FMT_32_32_32 = 0x2F,
   ADDR_FMT_32_32_32_FLOAT = 0x30,
   ADDR_FMT_BC1 = 0x31,
   ADDR_FMT_BC2 = 0x32,
   ADDR_FMT_BC3 = 0x33,
   ADDR_FMT_BC4 = 0x34,
   ADDR_FMT_BC5 = 0x35,
   ADDR_FMT_BC6 = 0x36,
   ADDR_FMT_BC7 = 0x37,
   ADDR_FMT_32_AS_32_32_32_32 = 0x38,
   ADDR_FMT_APC3 = 0x39,
   ADDR_FMT_APC4 = 0x3A,
   ADDR_FMT_APC5 = 0x3B,
   ADDR_FMT_APC6 = 0x3C,
   ADDR_FMT_APC7 = 0x3D,
   ADDR_FMT_CTX1 = 0x3E,
   ADDR_FMT_RESERVED_63 = 0x3F,
};


/**
***************************************************************************************************
*   AddrPipeCfg
*
*   @brief
*       The pipe configuration field specifies both the number of pipes and
*       how pipes are interleaved on the surface.
*       The expression of number of pipes, the shader engine tile size, and packer tile size
*       is encoded in a PIPE_CONFIG register field.
*       In general the number of pipes usually matches the number of memory channels of the
*       hardware configuration.
*       For hw configurations w/ non-pow2 memory number of memory channels, it usually matches
*       the number of ROP units(? TODO: which registers??)
*       The enum value = hw enum + 1 which is to reserve 0 for requesting default.
***************************************************************************************************
*/
enum AddrPipeCfg : uint32_t
{
   ADDR_PIPECFG_INVALID = 0x0,
   ADDR_PIPECFG_P2 = 0x1,
   ADDR_PIPECFG_P4_8x16 = 0x5,
   ADDR_PIPECFG_P4_16x16 = 0x6,
   ADDR_PIPECFG_P4_16x32 = 0x7,
   ADDR_PIPECFG_P4_32x32 = 0x8,
   ADDR_PIPECFG_P8_16x16_8x16 = 0x9,
   ADDR_PIPECFG_P8_16x32_8x16 = 0xA,
   ADDR_PIPECFG_P8_32x32_8x16 = 0xB,
   ADDR_PIPECFG_P8_16x32_16x16 = 0xC,
   ADDR_PIPECFG_P8_32x32_16x16 = 0xD,
   ADDR_PIPECFG_P8_32x32_16x32 = 0xE,
   ADDR_PIPECFG_P8_32x64_32x32 = 0xF,
   ADDR_PIPECFG_MAX = 0x10,
};


/**
***************************************************************************************************
* AddrTileType
*
*   @brief
*       Neutral enums that specifies micro tile type (MICRO_TILE_MODE)
***************************************************************************************************
*/
enum AddrTileType : uint32_t
{
   ADDR_DISPLAYABLE = 0x0,
   ADDR_NON_DISPLAYABLE = 0x1,
   ADDR_DEPTH_SAMPLE_ORDER = 0x2,
   ADDR_THICK_TILING = 0x3,
};


/**
***************************************************************************************************
*   AddrHtileBlockSize
*
*   @brief
*       Size of HTILE blocks, valid values are 4 or 8 for now
***************************************************************************************************
*/
enum AddrHtileBlockSize : uint32_t
{
   ADDR_HTILE_BLOCKSIZE_4 = 0x4,
   ADDR_HTILE_BLOCKSIZE_8 = 0x8,
};
