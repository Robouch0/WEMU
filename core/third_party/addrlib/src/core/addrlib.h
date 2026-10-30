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
* @file  addrlib.h
* @brief Contains the AddrLib base class definition.
***************************************************************************************************
*/

#pragma once
#include "addrlib/addrinterface.h"
#include "addrobject.h"
#include "addrelemlib.h"


/**
***************************************************************************************************
* @brief This class contains asic independent address lib functionalities
***************************************************************************************************
*/
class AddrLib : public AddrObject
{
public:
   AddrLib(ADDR_CLIENT_HANDLE hClient);
   virtual ~AddrLib() = default;

   static ADDR_E_RETURNCODE
   Create(const ADDR_CREATE_INPUT *pCreateIn, ADDR_CREATE_OUTPUT *pCreateOut);

   static AddrLib *
   GetAddrLib(ADDR_HANDLE hLib);

   void
   Destroy();

   AddrChipFamily
   GetAddrChipFamily();

   bool
   GetFillSizeFieldsFlags() const;

   uint32_t
   GetSliceComputingFlags() const;

   bool
   UseTileIndex(int32_t tileIndex) const;

   void
   SetAddrChipFamily(uint32_t uChipFamily, uint32_t uChipRevision);

   bool
   IsMacroTiled(AddrTileMode tileMode) const;

   uint32_t
   ComputeSurfaceThickness(AddrTileMode tileMode) const;

   void
   AdjustPitchAlignment(ADDR_SURFACE_FLAGS flags, uint32_t *pPitchAlign) const;

   void
   ComputeMipLevel(ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn) const;

   uint32_t
   PadDimensions(AddrTileMode tileMode,
                 ADDR_SURFACE_FLAGS flags,
                 uint32_t padDims,
                 uint32_t *pPitch,
                 uint32_t pitchAlign,
                 uint32_t *pHeight,
                 uint32_t heightAlign,
                 uint32_t *pSlices,
                 uint32_t sliceAlign) const;

   ADDR_E_RETURNCODE
   ComputeSurfaceInfo(ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                      ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const;

   uint64_t
   ComputeSurfaceAddrFromCoordLinear(uint32_t x,
                                     uint32_t y,
                                     uint32_t slice,
                                     uint32_t sample,
                                     uint32_t bpp,
                                     uint32_t pitch,
                                     uint32_t height,
                                     uint32_t numSlices,
                                     uint32_t *pBitPosition) const;

   uint32_t
   ComputePixelIndexWithinMicroTile(uint32_t x,
                                    uint32_t y,
                                    uint32_t z,
                                    uint32_t bpp,
                                    AddrTileMode tileMode,
                                    AddrTileType tileType) const;

   ADDR_E_RETURNCODE
   ComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                               ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const;

   ADDR_E_RETURNCODE
   ExtractBankPipeSwizzle(const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn,
                          ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut) const;

   void
   ComputeTileDataWidthAndHeight(uint32_t bpp,
                                 uint32_t cacheBits,
                                 ADDR_TILEINFO *pTileInfo,
                                 uint32_t *pMacroWidth,
                                 uint32_t *pMacroHeight) const;

   uint32_t
   ComputeHtileInfo(uint32_t pitchIn,
                     uint32_t heightIn,
                     uint32_t numSlices,
                     bool isLinear,
                     bool isWidth8,
                     bool isHeight8,
                     ADDR_TILEINFO *pTileInfo,
                     uint32_t *pPitchOut,
                     uint32_t *pHeightOut,
                     uint64_t *pHtileBytes,
                     uint32_t *pMacroWidth,
                     uint32_t *pMacroHeight,
                     uint64_t *pSliceSize,
                     uint32_t *pBaseAlign) const;

   ADDR_E_RETURNCODE
   ComputeHtileInfo(const ADDR_COMPUTE_HTILE_INFO_INPUT *pIn,
                     ADDR_COMPUTE_HTILE_INFO_OUTPUT *pOut) const;

   ADDR_E_RETURNCODE
   ComputeSliceTileSwizzle(const ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn,
                           ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut) const;

   virtual bool
   ComputeQbStereoInfo(ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const;

   virtual uint32_t
   GetNumPipes(ADDR_TILEINFO *pTileInfo) const;

   virtual void
   HwlComputeTileDataWidthAndHeightLinear(uint32_t *pMacroWidth,
                                          uint32_t *pMacroHeight,
                                          uint32_t bpp,
                                          ADDR_TILEINFO *pTileInfo) const;

   virtual bool
   HwlInitGlobalParams(const ADDR_CREATE_INPUT *pCreateIn) = 0;

   virtual bool
   HwlComputeMipLevel(ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const = 0;

   virtual ADDR_E_RETURNCODE
   HwlComputeSurfaceInfo(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                         ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const = 0;

   virtual ADDR_E_RETURNCODE
   HwlComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                  ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const = 0;

   virtual ADDR_E_RETURNCODE
   HwlSetupTileCfg(int32_t index,
                   ADDR_TILEINFO *pInfo,
                   AddrTileMode *mode,
                   AddrTileType *type) const = 0;

   virtual AddrChipFamily
   HwlConvertChipFamily(uint32_t uChipFamily,
                        uint32_t uChipRevision) = 0;

   virtual ADDR_E_RETURNCODE
   HwlExtractBankPipeSwizzle(const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn,
                             ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut) const = 0;

   virtual uint32_t
   HwlComputeHtileBpp(bool isWidth8,
                      bool isHeight8) const = 0;

   virtual uint32_t
   HwlComputeHtileBaseAlign(bool isLinear,
                            uint32_t pipes) const = 0;

   virtual uint64_t
   HwlComputeHtileBytes(uint32_t pitch,
                        uint32_t height,
                        uint32_t bpp,
                        bool isLinear,
                        uint32_t numSlices,
                        uint32_t baseAlign) const = 0;

   virtual ADDR_E_RETURNCODE
   HwlComputeSliceTileSwizzle(const ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn,
                              ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut) const = 0;

protected:
   AddrLibClass mClass;
   AddrChipFamily mChipFamily;
   uint32_t mChipRevision;
   uint32_t mVersion;
   ADDR_CONFIG_FLAGS mConfigFlags;

   AddrElemLib *mElemLib;

   uint32_t mPipes;
   uint32_t mBanks;
   uint32_t mPipeInterleaveBytes;
   uint32_t mRowSize;
};

AddrLib *
AddrR600HwlInit(ADDR_CLIENT_HANDLE hClient);
