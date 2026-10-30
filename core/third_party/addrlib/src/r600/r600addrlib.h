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
* @file  r600addrlib.h
* @brief Contains the R600AddrLib class definition.
***************************************************************************************************
*/

#pragma once
#include "core/addrlib.h"

enum PipeInterleaveSize
{
   ADDR_CONFIG_PIPE_INTERLEAVE_256B = 0,
   ADDR_CONFIG_PIPE_INTERLEAVE_512B = 1,
};

enum RowSize
{
   ADDR_CONFIG_1KB_ROW = 0,
   ADDR_CONFIG_2KB_ROW = 1,
   ADDR_CONFIG_4KB_ROW = 2,
   ADDR_CONFIG_8KB_ROW = 3,
   ADDR_CONFIG_1KB_ROW_OPT_BANK_SWAP = 4,
   ADDR_CONFIG_2KB_ROW_OPT_BANK_SWAP = 5,
   ADDR_CONFIG_4KB_ROW_OPT_BANK_SWAP = 6,
   ADDR_CONFIG_8KB_ROW_OPT_BANK_SWAP = 7,
};

enum BankSwapSize
{
   ADDR_CONFIG_BANK_SWAP_128B = 0,
   ADDR_CONFIG_BANK_SWAP_256B = 1,
   ADDR_CONFIG_BANK_SWAP_512B = 2,
   ADDR_CONFIG_BANK_SWAP_1024B = 3,
};

enum SampleSplitSize
{
   ADDR_CONFIG_SAMPLE_SPLIT_1KB = 0,
   ADDR_CONFIG_SAMPLE_SPLIT_2KB = 1,
   ADDR_CONFIG_SAMPLE_SPLIT_4KB = 2,
   ADDR_CONFIG_SAMPLE_SPLIT_8KB = 3,
};

union GB_TILING_CONFIG
{
   struct
   {
      uint32_t : 1;
      uint32_t pipe_tiling : 3;
      uint32_t bank_tiling : 2;
      uint32_t group_size : 2;
      uint32_t row_tiling : 3;
      uint32_t bank_swaps : 3;
      uint32_t sample_split : 2;
      uint32_t backend_map : 16;
   };

   uint32_t value;
};


/**
***************************************************************************************************
* @brief This class is the R600 specific address library
*        function set.
***************************************************************************************************
*/
class R600AddrLib : public AddrLib
{
public:
   R600AddrLib(ADDR_CLIENT_HANDLE hClient);
   virtual ~R600AddrLib() = default;

   static R600AddrLib *
   CreateObj(ADDR_CLIENT_HANDLE hClient);

   bool
   DecodeGbRegs(const ADDR_REGISTER_VALUE* pRegValue);

   virtual bool
   HwlInitGlobalParams(const ADDR_CREATE_INPUT *pCreateIn);

   virtual AddrChipFamily
   HwlConvertChipFamily(uint32_t uChipFamily, uint32_t uChipRevision) override;

   virtual bool
   HwlComputeMipLevel(ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const override;

   virtual ADDR_E_RETURNCODE
   HwlSetupTileCfg(int32_t index, ADDR_TILEINFO *pInfo, AddrTileMode *mode, AddrTileType *type) const override;

   AddrTileMode
   ConvertToNonBankSwappedMode(AddrTileMode tileMode) const;

   uint32_t
   ComputeSurfaceTileSlices(AddrTileMode tileMode,
                            uint32_t bpp,
                            uint32_t numSamples) const;

   uint32_t
   ComputeSurfaceRotationFromTileMode(AddrTileMode tileMode) const;

   AddrTileMode
   HwlDegradeThickTileMode(AddrTileMode tileMode,
                           uint32_t numSamples,
                           uint32_t tileSlices,
                           bool isDepth) const;

   AddrTileMode
   ComputeSurfaceMipLevelTileMode(AddrTileMode tileMode,
                                  uint32_t bpp,
                                  uint32_t level,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t numSlices,
                                  uint32_t numSamples,
                                  bool isDepth,
                                  bool noRecursive) const;

   bool
   ComputeSurfaceAlignmentsLinear(AddrTileMode tileMode,
                                  uint32_t bpp,
                                  ADDR_SURFACE_FLAGS flags,
                                  uint32_t *pBaseAlign,
                                  uint32_t *pPitchAlign,
                                  uint32_t *pHeightAlign) const;

   bool
   ComputeSurfaceAlignmentsMicrotiled(AddrTileMode tileMode,
                                      uint32_t bpp,
                                      ADDR_SURFACE_FLAGS flags,
                                      uint32_t numSamples,
                                      uint32_t *pBaseAlign,
                                      uint32_t *pPitchAlign,
                                      uint32_t *pHeightAlign) const;

   uint32_t
   ComputeMacroTileAspectRatio(AddrTileMode tileMode) const;

   bool
   IsDualBaseAlignNeeded(AddrTileMode tileMode) const;

   bool
   ComputeSurfaceAlignmentsMacrotiled(AddrTileMode tileMode,
                                      uint32_t bpp,
                                      ADDR_SURFACE_FLAGS flags,
                                      uint32_t numSamples,
                                      uint32_t *pBaseAlign,
                                      uint32_t *pPitchAlign,
                                      uint32_t *pHeightAlign,
                                      uint32_t *pMacroWidth,
                                      uint32_t *pMacroHeight) const;

   ADDR_E_RETURNCODE
   ComputeSurfaceInfoLinear(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                            ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                            uint32_t padDims,
                            AddrTileMode tileMode) const;

   ADDR_E_RETURNCODE
   ComputeSurfaceInfoMicroTiled(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                                uint32_t padDims,
                                AddrTileMode tileMode) const;

   bool
   IsThickMacroTiled(AddrTileMode tileMode) const;

   bool
   IsBankSwappedTileMode(AddrTileMode tileMode) const;

   bool
   IsDualPitchAlignNeeded(AddrTileMode tileMode,
                          bool isDepth,
                          uint32_t mipLevel) const;

   uint32_t
   ComputeSurfaceBankSwappedWidth(AddrTileMode tileMode,
                                  uint32_t bpp,
                                  uint32_t numSamples,
                                  uint32_t pitch,
                                  uint32_t *pSlicesPerTile) const;

   ADDR_E_RETURNCODE
   ComputeSurfaceInfoMacroTiled(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                                uint32_t padDims,
                                AddrTileMode tileMode,
                                AddrTileMode baseTileMode) const;

   virtual ADDR_E_RETURNCODE
   HwlComputeSurfaceInfo(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                         ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const override;

   AddrTileType
   GetTileType(bool isDepth) const;

   uint64_t
   ComputeSurfaceAddrFromCoordMicroTiled(uint32_t x,
                                         uint32_t y,
                                         uint32_t slice,
                                         uint32_t bpp,
                                         uint32_t pitch,
                                         uint32_t height,
                                         AddrTileMode tileMode,
                                         bool isDepth,
                                         uint32_t tileBase,
                                         uint32_t compBits,
                                         uint32_t *pBitPosition) const;

   uint32_t
   ComputePipeFromCoordWoRotation(uint32_t x, uint32_t y) const;

   uint32_t
   ComputeBankFromCoordWoRotation(uint32_t x, uint32_t y) const;

   uint64_t
   ComputeSurfaceAddrFromCoordMacroTiled(uint32_t x,
                                         uint32_t y,
                                         uint32_t slice,
                                         uint32_t sample,
                                         uint32_t bpp,
                                         uint32_t pitch,
                                         uint32_t height,
                                         uint32_t numSamples,
                                         AddrTileMode tileMode,
                                         bool isDepth,
                                         uint32_t tileBase,
                                         uint32_t compBits,
                                         uint32_t pipeSwizzle,
                                         uint32_t bankSwizzle,
                                         uint32_t *pBitPosition) const;

   uint64_t
   DispatchComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                       ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const;

   virtual ADDR_E_RETURNCODE
   HwlComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                  ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const override;

   void
   ExtractBankPipeSwizzle(uint32_t base256b,
                          uint32_t *pBankSwizzle,
                          uint32_t *pPipeSwizzle) const;

   uint32_t
   ComputeSliceTileSwizzle(AddrTileMode tileMode,
                           uint32_t baseSwizzle,
                           uint32_t slice,
                           size_t baseAddr) const;

   virtual ADDR_E_RETURNCODE
   HwlExtractBankPipeSwizzle(const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn,
                             ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut) const override;

   virtual uint32_t
   HwlComputeHtileBpp(bool isWidth8, bool isHeight8) const override;

   virtual uint32_t
   HwlComputeHtileBaseAlign(bool isLinear,
                            uint32_t pipes) const override;

   virtual uint64_t
   HwlComputeHtileBytes(uint32_t pitch,
                        uint32_t height,
                        uint32_t bpp,
                        bool isLinear,
                        uint32_t numSlices,
                        uint32_t baseAlign) const override;

   virtual ADDR_E_RETURNCODE
   HwlComputeSliceTileSwizzle(const ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn,
                              ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut) const override;

private:
   uint32_t mSwapSize;
   uint32_t mSplitSize;
};
