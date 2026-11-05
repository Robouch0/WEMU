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
* @file  addrcommon.h
* @brief Contains the helper function and constants
***************************************************************************************************
*/

#pragma once
#include <cstdint>
#include "addrlib/addrinterface.h"

static const uint32_t MicroTileWidth = 8;
static const uint32_t MicroTileHeight = 8;
static const uint32_t ThickTileThickness = 4;
static const uint32_t XThickTileThickness = 8;
static const uint32_t HtileCacheBits = 16384;
static const uint32_t MicroTilePixels = MicroTileWidth * MicroTileHeight;

static const int32_t TileIndexInvalid = TILEINDEX_INVALID;
static const int32_t TileIndexLinearGeneral = TILEINDEX_LINEAR_GENERAL;
static const int32_t TileIndexNoMacroIndex = -3;

#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(x) (((x) + (BITS_PER_BYTE-1)) / BITS_PER_BYTE)
#define BYTES_TO_BITS(x) ((x) * BITS_PER_BYTE)
#define _BIT(v,b) (((v) >> (b)) & 1)


/**
***************************************************************************************************
* @brief Enums to identify AddrLib type
***************************************************************************************************
*/
enum AddrLibClass : uint32_t
{
   BASE_ADDRLIB = 0x0,
   R600_ADDRLIB = 0x6,
   R800_ADDRLIB = 0x8,
   SI_ADDRLIB = 0xA,
};


/**
***************************************************************************************************
* AddrChipFamily
*
*   @brief
*       Neutral enums that specifies chip family.
***************************************************************************************************
*/
enum AddrChipFamily : uint32_t
{
   ADDR_CHIP_FAMILY_IVLD = 0x0,
   ADDR_CHIP_FAMILY_R6XX = 0x1,
   ADDR_CHIP_FAMILY_R7XX = 0x2,
   ADDR_CHIP_FAMILY_R8XX = 0x3,
   ADDR_CHIP_FAMILY_R9XX = 0x4,
   ADDR_CHIP_FAMILY_SI = 0x5,
};


/**
***************************************************************************************************
* ADDR_CONFIG_FLAGS
*
*   @brief
*       This structure is used to set addr configuration flags.
***************************************************************************************************
*/
union ADDR_CONFIG_FLAGS
{
   struct
   {
      uint32_t forceLinearAligned : 1;
      uint32_t optimalBankSwap : 1;
      uint32_t no1DTiledMSAA : 1;
      uint32_t noCubeMipSlicesPad : 1;
      uint32_t sliceSizeComputing : 2;
      uint32_t fillSizeFields : 1;
      uint32_t useTileIndex : 1;
      uint32_t useTileCaps : 1;
   };

   uint32_t value;
};


/**
***************************************************************************************************
*   IsPow2
*
*   @brief
*       Check if the size is pow 2
***************************************************************************************************
*/
template<typename Type>
constexpr inline Type
IsPow2(Type dim)
{
   return !(dim & (dim - 1));
}


/**
***************************************************************************************************
*   ByteAlign
*
*   @brief
*       Align "x" to "align" alignment, "align" should be power of 2
***************************************************************************************************
*/
template<typename Type>
constexpr inline Type
PowTwoAlign(Type x, Type align)
{
   return (x + (align - 1)) & (~(align - 1));
}


/**
***************************************************************************************************
*   NextPow2
*
*   @brief
*       Compute the mipmap's next level dim size
***************************************************************************************************
*/
inline uint32_t
NextPow2(uint32_t dim)
{
   uint32_t newDim = 1;

   if (dim > 0x7fffffff) {
      newDim = 0x80000000;
   } else {
      while (newDim < dim) {
         newDim <<= 1;
      }
   }

   return newDim;
}


/**
***************************************************************************************************
*   Log2
*
*   @brief
*       Compute log of base 2
***************************************************************************************************
*/
template<typename Type>
inline Type
Log2(Type x)
{
   Type y = 0;

   while (x > 1) {
      x >>= 1;
      y++;
   }

   return y;
}
