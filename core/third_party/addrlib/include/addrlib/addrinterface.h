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
* @file  addrinterface.h
* @brief Contains the addrlib interfaces declaration and parameter defines
***************************************************************************************************
*/

#pragma once
#include <cstdarg>
#include <cstddef>
#include "addrtypes.h"

#define ADDRLIB_VERSION 502

using ADDR_CLIENT_HANDLE = void *;
using ADDR_HANDLE = void *;


/**
***************************************************************************************************
* @brief Alloc system memory flags.
* @note These flags are reserved for future use and if flags are added will minimize the impact
*       of the client.
***************************************************************************************************
*/
union ADDR_ALLOCSYSMEM_FLAGS
{
   uint32_t value;
};


/**
***************************************************************************************************
* @brief Alloc system memory input structure
***************************************************************************************************
*/
struct ADDR_ALLOCSYSMEM_INPUT
{
   uint32_t size;
   ADDR_ALLOCSYSMEM_FLAGS flags;
   uint32_t sizeInBytes;
   ADDR_CLIENT_HANDLE hClient;
};


/**
***************************************************************************************************
* @brief Free system memory input structure
***************************************************************************************************
*/
struct ADDR_FREESYSMEM_INPUT
{
   uint32_t size;
   void *pVirtAddr;
   ADDR_CLIENT_HANDLE hClient;
};


/**
***************************************************************************************************
* @brief Print debug message input structure
***************************************************************************************************
*/
struct ADDR_DEBUGPRINT_INPUT
{
   uint32_t size;
   char *pDebugString;
   va_list ap;
   ADDR_CLIENT_HANDLE hClient;
};


/**
***************************************************************************************************
* ADDR_ALLOCSYSMEM
*   @brief
*       Allocate system memory callback function. Returns valid pointer on success.
***************************************************************************************************
*/
using ADDR_ALLOCSYSMEM = void *(*)(const ADDR_ALLOCSYSMEM_INPUT *pInput);


/**
***************************************************************************************************
* ADDR_FREESYSMEM
*   @brief
*       Free system memory callback function.
*       Returns ADDR_OK on success.
***************************************************************************************************
*/
using ADDR_FREESYSMEM = ADDR_E_RETURNCODE(*)(const ADDR_FREESYSMEM_INPUT *pInput);


/**
***************************************************************************************************
* ADDR_DEBUGPRINT
*   @brief
*       Print debug message callback function.
*       Returns ADDR_OK on success.
***************************************************************************************************
*/
using ADDR_DEBUGPRINT = ADDR_E_RETURNCODE(*)(const ADDR_DEBUGPRINT_INPUT *pInput);


/**
***************************************************************************************************
* ADDR_CALLBACKS
*
*   @brief
*       Address Library needs client to provide system memory alloc/free routines.
***************************************************************************************************
*/
struct ADDR_CALLBACKS
{
   ADDR_ALLOCSYSMEM allocSysMem;
   ADDR_FREESYSMEM freeSysMem;
   ADDR_DEBUGPRINT debugPrint;
};


/**
***************************************************************************************************
* ADDR_CREATE_FLAGS
*
*   @brief
*       This structure is used to pass some setup in creation of AddrLib
*   @note
***************************************************************************************************
*/
union ADDR_CREATE_FLAGS
{
   struct
   {
      uint32_t forceLinearAligned : 1;
      uint32_t noCubeMipSlicesPad : 1;
      uint32_t sliceSizeComputing : 1;
      uint32_t fillSizeFields : 1;
      uint32_t useTileIndex : 1;
      uint32_t useTileCaps : 1;
   };

   uint32_t value;
};


/**
***************************************************************************************************
*   ADDR_REGISTER_VALUE
*
*   @brief
*       Data from registers to setup AddrLib global data, used in AddrCreate
***************************************************************************************************
*/
struct ADDR_REGISTER_VALUE
{
   uint32_t gbAddrConfig;
   uint32_t backendDisables;
   uint32_t backendMap;
   uint32_t noOfBanks;
   uint32_t noOfRanks;
   const uint32_t *pTileConfig;
   uint32_t noOfEntries;
};

union ADDR_TILE_CAPS
{
   struct
   {
      uint32_t : 2;
      uint32_t tile1DThin1 : 1;
      uint32_t tile1DThick : 1;
      uint32_t tile2DThin1 : 1;
      uint32_t tile2DThin2 : 1;
      uint32_t tile2DThin4 : 1;
      uint32_t tile2DThick : 1;
      uint32_t tile2BThin1 : 1;
      uint32_t tile2BThin2 : 1;
      uint32_t tile2BThin4 : 1;
      uint32_t tile2BThick : 1;
      uint32_t tile3DThin1 : 1;
      uint32_t tile3DThick : 1;
      uint32_t tile3BThin1 : 1;
      uint32_t tile3BThick : 1;
      uint32_t tile2DXThick : 1;
      uint32_t tile3DXThick : 1;
      uint32_t tilePowerSave : 1;
   };

   uint32_t value;
};


/**
***************************************************************************************************
* ADDR_CREATE_INPUT
*
*   @brief
*       Parameters use to create an AddrLib Object. Caller must provide all fields.
*
***************************************************************************************************
*/
struct ADDR_CREATE_INPUT
{
   uint32_t size;
   uint32_t chipEngine;
   uint32_t chipFamily;
   uint32_t chipRevision;
   ADDR_CALLBACKS callbacks;
   ADDR_CREATE_FLAGS createFlags;
   ADDR_REGISTER_VALUE regValue;
   ADDR_CLIENT_HANDLE hClient;
   ADDR_TILE_CAPS tileCaps;
};


/**
***************************************************************************************************
* ADDR_CREATEINFO_OUTPUT
*
*   @brief
*       Return AddrLib handle to client driver
*
***************************************************************************************************
*/
struct ADDR_CREATE_OUTPUT
{
   uint32_t size;
   ADDR_HANDLE hLib;
};


/**
***************************************************************************************************
*   ADDR_SURFACE_FLAGS
*
*   @brief
*       Surface flags
***************************************************************************************************
*/
union ADDR_SURFACE_FLAGS
{
   struct
   {
      uint32_t color : 1;
      uint32_t depth : 1;
      uint32_t stencil : 1;
      uint32_t texture : 1;
      uint32_t cube : 1;
      uint32_t volume : 1;
      uint32_t fmask : 1;
      uint32_t cubeAsArray : 1;
      uint32_t compressZ : 1;
      uint32_t linearWA : 1;
      uint32_t overlay : 1;
      uint32_t noStencil : 1;
      uint32_t inputBaseMap : 1;
      uint32_t display : 1;
      uint32_t opt4Space : 1;
      uint32_t prt : 1;
      uint32_t qbStereo : 1;
      uint32_t pow2Pad : 1;
      uint32_t reserved : 1;
   };

   uint32_t value;
};


/**
***************************************************************************************************
* @brief
*       Bank/tiling parameters. On function input, these can be set as desired or
*       left 0 for AddrLib to calculate/default. On function output, these are the actual
*       parameters used.
* @note
*       Valid bankWidth/bankHeight value:
*       1,2,4,8. They are factors instead of pixels or bytes.
*
*       The bank number remains constant across each row of the
*       macro tile as each pipe is selected, so the number of
*       tiles in the x direction with the same bank number will
*       be bank_width * num_pipes.
***************************************************************************************************
*/
struct ADDR_TILEINFO
{
   uint32_t banks;
   uint32_t bankWidth;
   uint32_t bankHeight;
   uint32_t macroAspectRatio;
   uint32_t tileSplitBytes;
   AddrPipeCfg pipeConfig;
};


/**
***************************************************************************************************
* @brief
*       Information needed by quad buffer stereo support
***************************************************************************************************
*/
struct ADDR_QBSTEREOINFO
{
   uint32_t eyeHeight;
   uint32_t rightOffset;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_INFO_INPUT
*
*   @brief
*       Input structure for AddrComputeSurfaceInfo
***************************************************************************************************
*/
struct ADDR_COMPUTE_SURFACE_INFO_INPUT
{
   uint32_t size;
   AddrTileMode tileMode;
   AddrFormat format;
   uint32_t bpp;
   uint32_t numSamples;
   uint32_t width;
   uint32_t height;
   uint32_t numSlices;
   uint32_t slice;
   uint32_t mipLevel;
   ADDR_SURFACE_FLAGS flags;
   uint32_t numFrags;
   ADDR_TILEINFO *pTileInfo;
   AddrTileType tileType;
   int32_t tileIndex;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_INFO_OUTPUT
*
*   @brief
*       Output structure for AddrComputeSurfInfo
*   @note
*       Element: AddrLib unit for computing. e.g. BCn: 4x4 blocks; R32B32B32: 32bit with 3x pitch
*       Pixel: Original pixel
***************************************************************************************************
*/
struct ADDR_COMPUTE_SURFACE_INFO_OUTPUT
{
   uint32_t size;
   uint32_t pitch;
   uint32_t height;
   uint32_t depth;
   uint64_t surfSize;
   AddrTileMode tileMode;
   uint32_t baseAlign;
   uint32_t pitchAlign;
   uint32_t heightAlign;
   uint32_t depthAlign;
   uint32_t bpp;
   uint32_t pixelPitch;
   uint32_t pixelHeight;
   uint32_t pixelBits;
   uint32_t sliceSize;
   uint32_t pitchTileMax;
   uint32_t heightTileMax;
   uint32_t sliceTileMax;
   ADDR_TILEINFO *pTileInfo;
   AddrTileType tileType;
   int32_t tileIndex;
   ADDR_QBSTEREOINFO *pStereoInfo;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT
*
*   @brief
*       Input structure for AddrComputeSurfaceAddrFromCoord
***************************************************************************************************
*/
struct ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT
{
   uint32_t size;
   uint32_t x;
   uint32_t y;
   uint32_t slice;
   uint32_t sample;
   uint32_t bpp;
   uint32_t pitch;
   uint32_t height;
   uint32_t numSlices;
   uint32_t numSamples;
   AddrTileMode tileMode;
   bool isDepth;
   uint32_t tileBase;
   uint32_t compBits;
   uint32_t pipeSwizzle;
   uint32_t bankSwizzle;
   uint32_t numFrags;
   AddrTileType tileType;
   bool ignoreSE;
   uint32_t __pad;
   ADDR_TILEINFO *pTileInfo;
   int32_t tileIndex;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT
*
*   @brief
*       Output structure for AddrComputeSurfaceAddrFromCoord
***************************************************************************************************
*/
struct ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT
{
   uint32_t size;
   uint64_t addr;
   uint32_t bitPosition;
};


/**
***************************************************************************************************
*   ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrExtractBankPipeSwizzle
***************************************************************************************************
*/
struct ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT
{
   uint32_t size;
   uint32_t base256b;
   ADDR_TILEINFO *pTileInfo;
   int32_t tileIndex;
};


/**
***************************************************************************************************
*   ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrExtractBankPipeSwizzle
***************************************************************************************************
*/
struct ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT
{
   uint32_t size;
   uint32_t bankSwizzle;
   uint32_t pipeSwizzle;
};



/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_INFO_INPUT
*
*   @brief
*       Input structure of AddrComputeHtileInfo
***************************************************************************************************
*/
struct ADDR_COMPUTE_HTILE_INFO_INPUT
{
   uint32_t size;
   uint32_t pitch;
   uint32_t height;
   uint32_t numSlices;
   bool isLinear;
   AddrHtileBlockSize blockWidth;
   AddrHtileBlockSize blockHeight;
   ADDR_TILEINFO *pTileInfo;
   int32_t tileIndex;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_HTILE_INFO_OUTPUT
*
*   @brief
*       Output structure of AddrComputeHtileInfo
***************************************************************************************************
*/
struct ADDR_COMPUTE_HTILE_INFO_OUTPUT
{
   uint32_t size;
   uint32_t pitch;
   uint32_t height;
   uint64_t htileBytes;
   uint32_t baseAlign;
   uint32_t bpp;
   uint32_t macroWidth;
   uint32_t macroHeight;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SLICESWIZZLE_INPUT
*
*   @brief
*       Input structure of AddrComputeSliceSwizzle
***************************************************************************************************
*/
struct ADDR_COMPUTE_SLICESWIZZLE_INPUT
{
   uint32_t size;
   AddrTileMode tileMode;
   uint32_t baseSwizzle;
   uint32_t slice;
   size_t baseAddr;
   ADDR_TILEINFO *pTileInfo;
   int32_t tileIndex;
};


/**
***************************************************************************************************
*   ADDR_COMPUTE_SLICESWIZZLE_OUTPUT
*
*   @brief
*       Output structure of AddrComputeSliceSwizzle
***************************************************************************************************
*/
struct ADDR_COMPUTE_SLICESWIZZLE_OUTPUT
{
   uint32_t size;
   uint32_t tileSwizzle;
};


/**
***************************************************************************************************
*   AddrCreate
*
*   @brief
*       Create AddrLib object, must be called before any interface calls
*
*   @return
*       ADDR_OK if successful
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrCreate(const ADDR_CREATE_INPUT *pCreateIn, ADDR_CREATE_OUTPUT *pCreateOut);


/**
***************************************************************************************************
*   AddrDestroy
*
*   @brief
*       Destroy AddrLib object, must be called to free internally allocated resources.
*
*   @return
*      ADDR_OK if successful
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrDestroy(ADDR_HANDLE hLib);


/**
***************************************************************************************************
*   AddrComputeSurfaceInfo
*
*   @brief
*       Compute surface width/height/depth/alignments and suitable tiling mode
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeSurfaceInfo(ADDR_HANDLE hLib, ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn, ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut);


/**
***************************************************************************************************
*   AddrComputeSurfaceAddrFromCoord
*
*   @brief
*       Compute surface address from a given coordinate.
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeSurfaceAddrFromCoord(ADDR_HANDLE hLib, ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn, ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut);


/**
***************************************************************************************************
*   AddrExtractBankPipeSwizzle
*
*   @brief
*       Extract Bank and Pipe swizzle from base256b
*   @return
*       ADDR_OK if no error
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrExtractBankPipeSwizzle(ADDR_HANDLE hLib, ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn, ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut);


/**
***************************************************************************************************
*   AddrComputeHtileInfo
*
*   @brief
*       Compute Htile pitch, height, base alignment and size in bytes
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeHtileInfo(ADDR_HANDLE hLib, ADDR_COMPUTE_HTILE_INFO_INPUT *pIn, ADDR_COMPUTE_HTILE_INFO_OUTPUT *pOut);


/**
***************************************************************************************************
*   AddrComputeSliceSwizzle
*
*   @brief
*       Compute a swizzle for slice from a base swizzle
*   @return
*       ADDR_OK if no error
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeSliceSwizzle(ADDR_HANDLE hLib, ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn, ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut);
