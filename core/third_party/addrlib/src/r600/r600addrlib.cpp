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
* @file  r600addrlib.cpp
* @brief Contains the implementation for the R600AddrLib class.
***************************************************************************************************
*/

#include <algorithm>
#include <new>
#include "r600addrlib.h"


/**
***************************************************************************************************
*   AddrR600HwlInit
*
*   @brief
*       Creates an R600AddrLib object.
*
*   @return
*       Returns an R600AddrLib object pointer.
***************************************************************************************************
*/
AddrLib *
AddrR600HwlInit(ADDR_CLIENT_HANDLE hClient)
{
   return R600AddrLib::CreateObj(hClient);
}


/**
***************************************************************************************************
*   R600AddrLib::R600AddrLib
*
*   @brief
*       Constructor
*
***************************************************************************************************
*/
R600AddrLib::R600AddrLib(ADDR_CLIENT_HANDLE hClient) :
   AddrLib(hClient),
   mSwapSize(0),
   mSplitSize(0)
{
   mClass = R600_ADDRLIB;
}


/**
***************************************************************************************************
*   R600AddrLib::CreateObj
*
*   @brief
*       Creates an R600AddrLib object.
*
*   @return
*       Returns an R600AddrLib object pointer.
***************************************************************************************************
*/
R600AddrLib *
R600AddrLib::CreateObj(ADDR_CLIENT_HANDLE hClient)
{
   auto memory = AddrObject::ClientAlloc(sizeof(R600AddrLib), hClient);
   return new (memory) R600AddrLib(hClient);
}


/**
***************************************************************************************************
*   R600AddrLib::DecodeGbRegs
*
*   @brief
*       Decodes GB_TILING_CONFIG
*
*   @return
*       true if all settings are valid
*
***************************************************************************************************
*/
bool
R600AddrLib::DecodeGbRegs(const ADDR_REGISTER_VALUE* pRegValue)
{
   GB_TILING_CONFIG reg;
   bool valid = true;

   reg.value = pRegValue->gbAddrConfig;

   switch (reg.pipe_tiling) {
   case 0:
      mPipes = 1;
      break;
   case 1:
      mPipes = 2;
      break;
   case 2:
      mPipes = 4;
      break;
   case 3:
      mPipes = 8;
      break;
   default:
      valid = false;
   }

   switch (reg.bank_tiling) {
   case 0:
      mBanks = 4;
      break;
   case 1:
      mBanks = 8;
      break;
   default:
      valid = false;
   }

   switch (reg.group_size) {
   case ADDR_CONFIG_PIPE_INTERLEAVE_256B:
      mPipeInterleaveBytes = 256;
      break;
   case ADDR_CONFIG_PIPE_INTERLEAVE_512B:
      mPipeInterleaveBytes = 512;
      break;
   default:
      valid = false;
   }

   switch (reg.row_tiling) {
   case ADDR_CONFIG_1KB_ROW:
      mRowSize = 1024;
      break;
   case ADDR_CONFIG_2KB_ROW:
      mRowSize = 2048;
      break;
   case ADDR_CONFIG_4KB_ROW:
      mRowSize = 4096;
      break;
   case ADDR_CONFIG_8KB_ROW:
      mRowSize = 8192;
      break;
   case ADDR_CONFIG_1KB_ROW_OPT_BANK_SWAP:
      mConfigFlags.optimalBankSwap = 1;
      mRowSize = 1024;
      break;
   case ADDR_CONFIG_2KB_ROW_OPT_BANK_SWAP:
      mConfigFlags.optimalBankSwap = 1;
      mRowSize = 2048;
      break;
   case ADDR_CONFIG_4KB_ROW_OPT_BANK_SWAP:
      mConfigFlags.optimalBankSwap = 1;
      mRowSize = 4096;
      break;
   case ADDR_CONFIG_8KB_ROW_OPT_BANK_SWAP:
      mConfigFlags.optimalBankSwap = 1;
      mRowSize = 8192;
      break;
   default:
      valid = false;
   }

   switch (reg.bank_swaps) {
   case ADDR_CONFIG_BANK_SWAP_128B:
      mSwapSize = 128;
      break;
   case ADDR_CONFIG_BANK_SWAP_256B:
      mSwapSize = 256;
      break;
   case ADDR_CONFIG_BANK_SWAP_512B:
      mSwapSize = 512;
      break;
   case ADDR_CONFIG_BANK_SWAP_1024B:
      mSwapSize = 1024;
      break;
   default:
      valid = false;
   }

   switch (reg.sample_split) {
   case ADDR_CONFIG_SAMPLE_SPLIT_1KB:
      mSplitSize = 1024;
      break;
   case ADDR_CONFIG_SAMPLE_SPLIT_2KB:
      mSplitSize = 2048;
      break;
   case ADDR_CONFIG_SAMPLE_SPLIT_4KB:
      mSplitSize = 4096;
      break;
   case ADDR_CONFIG_SAMPLE_SPLIT_8KB:
      mSplitSize = 8192;
      break;
   default:
      valid = false;
   }

   return valid;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlInitGlobalParams
*
*   @brief
*       Initializes global parameters
*
*   @return
*       true if all settings are valid
*
***************************************************************************************************
*/
bool
R600AddrLib::HwlInitGlobalParams(const ADDR_CREATE_INPUT *pCreateIn)
{
   auto valid = DecodeGbRegs(&pCreateIn->regValue);
   mConfigFlags.no1DTiledMSAA = 1;
   return valid;
}


/**
***************************************************************************************************
*   SIAddrLib::HwlConvertChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to AddrChipFamily
*   @return
*       AddrChipFamily
***************************************************************************************************
*/
AddrChipFamily
R600AddrLib::HwlConvertChipFamily(uint32_t uChipFamily, uint32_t uChipRevision)
{
   AddrChipFamily family;

   // Just a guess of what the FAMILY_ names could be...
   switch (uChipFamily) {
   case  0x46: // FAMILY_RV620
   case  0x47: // FAMILY_RV635
   case  0x48: // FAMILY_RV670
   case  0x4B: // FAMILY_RV680
      family = ADDR_CHIP_FAMILY_R6XX;
      break;
   case  0x51: // FAMILY_RV710
   case  0x52: // FAMILY_RV730
   case  0x55: // FAMILY_RV740
      family = ADDR_CHIP_FAMILY_R7XX;
      break;
   }

   if (family == ADDR_CHIP_FAMILY_R6XX) {
      mConfigFlags.noCubeMipSlicesPad = 1;
   }

   return family;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeMipLevel
*   @brief
*       Compute MipLevel info (including level 0)
*   @return
*       TRUE if HWL's handled
***************************************************************************************************
*/
bool
R600AddrLib::HwlComputeMipLevel(ADDR_COMPUTE_SURFACE_INFO_INPUT* pIn) const
{
   if (!mElemLib->IsBlockCompressed(pIn->format)) {
      return false;
   }

   if (pIn->mipLevel > 0) {
      auto width = pIn->width;
      auto height = pIn->height;
      auto slices = pIn->numSlices;

      if (pIn->flags.inputBaseMap) {
         width = std::max<uint32_t>(1u, width >> pIn->mipLevel);
         height = std::max<uint32_t>(1u, height >> pIn->mipLevel);

         if (!pIn->flags.cube) {
            slices = std::max<uint32_t>(1u, slices >> pIn->mipLevel);
         } else {
            slices = std::max<uint32_t>(1u, slices);
         }
      }

      pIn->width = NextPow2(width);
      pIn->height = NextPow2(height);
      pIn->numSlices = slices;
   }

   return true;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlSetupTileCfg
*
*   @brief
*       Map tile index to tile setting.
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::HwlSetupTileCfg(int32_t index,
                             ADDR_TILEINFO *pInfo,
                             AddrTileMode *mode,
                             AddrTileType *type) const
{
   return ADDR_OK;
}


/**
***************************************************************************************************
*   R600AddrLib::ConvertToNonBankSwappedMode
*
*   @brief
*       Convert bank swapped tile mode to it's non-banked swap equivalent.
*   @return
*       Non-bank swapped AddrTileMode
***************************************************************************************************
*/
AddrTileMode
R600AddrLib::ConvertToNonBankSwappedMode(AddrTileMode tileMode) const
{
   switch (tileMode) {
   case ADDR_TM_2B_TILED_THIN1:
      return ADDR_TM_2D_TILED_THIN1;
   case ADDR_TM_2B_TILED_THIN2:
      return ADDR_TM_2D_TILED_THIN2;
   case ADDR_TM_2B_TILED_THIN4:
      return ADDR_TM_2D_TILED_THIN4;
   case ADDR_TM_2B_TILED_THICK:
      return ADDR_TM_2D_TILED_THICK;
   case ADDR_TM_3B_TILED_THIN1:
      return ADDR_TM_3D_TILED_THIN1;
   case ADDR_TM_3B_TILED_THICK:
      return ADDR_TM_3D_TILED_THICK;
   default:
      return tileMode;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceTileSlices
*
*   @brief
*       Compute number of tile slices for a surface.
*   @return
*       Number of tile slices.
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeSurfaceTileSlices(AddrTileMode tileMode,
                                      uint32_t bpp,
                                      uint32_t numSamples) const
{
   auto bytesPerSample = BITS_TO_BYTES(bpp * 64);
   auto tileSlices = 1u;

   if (AddrLib::ComputeSurfaceThickness(tileMode) > 1) {
      numSamples = 4;
   }

   if (bytesPerSample) {
      auto samplePerTile = mSplitSize / bytesPerSample;

      if (samplePerTile) {
         tileSlices = std::max<uint32_t>(1u, numSamples / samplePerTile);
      }
   }

   return tileSlices;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceRotationFromTileMode
*
*   @brief
*       Compute surface rotation for an AddrTileMode
*   @return
*       Rotation
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeSurfaceRotationFromTileMode(AddrTileMode tileMode) const
{
   uint32_t rotate = 0;

   switch (tileMode) {
   case ADDR_TM_2D_TILED_THIN1:
   case ADDR_TM_2D_TILED_THIN2:
   case ADDR_TM_2D_TILED_THIN4:
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_2B_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN4:
   case ADDR_TM_2B_TILED_THICK:
      rotate = mPipes * ((mBanks >> 1) - 1);
      break;
   case ADDR_TM_3D_TILED_THIN1:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THIN1:
   case ADDR_TM_3B_TILED_THICK:
      if (mPipes >= 4) {
         rotate = (mPipes >> 1) - 1;
      } else {
         rotate = 1;
      }
      break;
   default:
      rotate = 0;
   }

   return rotate;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlDegradeThickTileMode
*
*   @brief
*       Degrades valid tile mode for thick modes if needed
*
*   @return
*       Suitable tile mode
***************************************************************************************************
*/
AddrTileMode
R600AddrLib::HwlDegradeThickTileMode(AddrTileMode tileMode,
                                     uint32_t numSamples,
                                     uint32_t tileSlices,
                                     bool isDepth) const
{
   switch (tileMode) {
   case ADDR_TM_1D_TILED_THIN1:
      if (numSamples > 1 && mConfigFlags.no1DTiledMSAA) {
         tileMode = ADDR_TM_2D_TILED_THIN1;
      }
      break;
   case ADDR_TM_1D_TILED_THICK:
      if (numSamples > 1 || isDepth) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
      }

      if (numSamples == 2 || numSamples == 4) {
         tileMode = ADDR_TM_2D_TILED_THICK;
      }
      break;
   case ADDR_TM_2D_TILED_THIN2:
      if (2 * mPipeInterleaveBytes > mSplitSize) {
         tileMode = ADDR_TM_2D_TILED_THIN1;
      }
      break;
   case ADDR_TM_2D_TILED_THIN4:
      if (4 * mPipeInterleaveBytes > mSplitSize) {
         tileMode = ADDR_TM_2D_TILED_THIN2;
      }
      break;
   case ADDR_TM_2D_TILED_THICK:
      if (numSamples > 1 || tileSlices > 1 || isDepth) {
         tileMode = ADDR_TM_2D_TILED_THIN1;
      }
      break;
   case ADDR_TM_2B_TILED_THIN2:
      if (2 * mPipeInterleaveBytes > mSplitSize) {
         tileMode = ADDR_TM_2B_TILED_THIN1;
      }
      break;
   case ADDR_TM_2B_TILED_THIN4:
      if (4 * mPipeInterleaveBytes > mSplitSize) {
         tileMode = ADDR_TM_2B_TILED_THIN2;
      }
      break;
   case ADDR_TM_2B_TILED_THICK:
      if (numSamples > 1 || tileSlices > 1 || isDepth) {
         tileMode = ADDR_TM_2B_TILED_THIN1;
      }
      break;
   case ADDR_TM_3D_TILED_THICK:
      if (numSamples > 1 || tileSlices > 1 || isDepth) {
         tileMode = ADDR_TM_3D_TILED_THIN1;
      }
      break;
   case ADDR_TM_3B_TILED_THICK:
      if (numSamples > 1 || tileSlices > 1 || isDepth) {
         tileMode = ADDR_TM_3B_TILED_THIN1;
      }
      break;
   }

   return tileMode;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceMipLevelTileMode
*
*   @brief
*       Compute valid tile mode for surface mipmap sub-levels
*
*   @return
*       Suitable tile mode
***************************************************************************************************
*/
AddrTileMode
R600AddrLib::ComputeSurfaceMipLevelTileMode(AddrTileMode baseTileMode,
                                            uint32_t bpp,
                                            uint32_t level,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t numSlices,
                                            uint32_t numSamples,
                                            bool isDepth,
                                            bool noRecursive) const
{
   auto tileSlices = ComputeSurfaceTileSlices(baseTileMode, bpp, numSamples);
   auto tileMode = HwlDegradeThickTileMode(baseTileMode, numSamples, tileSlices, isDepth);
   auto rotation = ComputeSurfaceRotationFromTileMode(tileMode);

   if ((rotation % mPipes) == 0) {
      switch (tileMode) {
      case ADDR_TM_3D_TILED_THIN1:
         tileMode = ADDR_TM_2D_TILED_THIN1;
         break;
      case ADDR_TM_3D_TILED_THICK:
         tileMode = ADDR_TM_2D_TILED_THICK;
         break;
      case ADDR_TM_3B_TILED_THIN1:
         tileMode = ADDR_TM_2B_TILED_THIN1;
         break;
      case ADDR_TM_3B_TILED_THICK:
         tileMode = ADDR_TM_2B_TILED_THICK;
         break;
      }
   }

   if (noRecursive || level == 0) {
      return tileMode;
   }

   if (bpp == 96 || bpp == 48 || bpp == 24) {
      bpp /= 3;
   }

   width = NextPow2(width);
   height = NextPow2(height);
   numSlices = NextPow2(numSlices);

   tileMode = ConvertToNonBankSwappedMode(tileMode);

   auto thickness = ComputeSurfaceThickness(tileMode);
   auto microTileBytes = BITS_TO_BYTES(numSamples * bpp * thickness * 64);
   auto widthAlignFactor = 1u;

   if (microTileBytes <= mPipeInterleaveBytes) {
      widthAlignFactor = mPipeInterleaveBytes / microTileBytes;
   }

   auto macroTileWidth = 8 * mBanks;
   auto macroTileHeight = 8 * mPipes;

   // Reduce the tile mode from 2D/3D to 1D in following conditions
   switch (tileMode) {
   case ADDR_TM_2D_TILED_THIN1:
   case ADDR_TM_3D_TILED_THIN1:
      if (width < widthAlignFactor * macroTileWidth || height < macroTileHeight) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
      }
      break;
   case ADDR_TM_2D_TILED_THIN2:
      macroTileWidth >>= 1;
      macroTileHeight *= 2;

      if (width < widthAlignFactor * macroTileWidth || height < macroTileHeight) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
      }
      break;
   case ADDR_TM_2D_TILED_THIN4:
      macroTileWidth >>= 2;
      macroTileHeight *= 4;

      if (width < widthAlignFactor * macroTileWidth || height < macroTileHeight) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
      }
      break;
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_3D_TILED_THICK:
      if (width < widthAlignFactor * macroTileWidth || height < macroTileHeight) {
         tileMode = ADDR_TM_1D_TILED_THICK;
      }
      break;
   }

   if (tileMode == ADDR_TM_1D_TILED_THICK) {
      if (numSlices < 4) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
      }
   } else if (tileMode == ADDR_TM_2D_TILED_THICK) {
      if (numSlices < 4) {
         tileMode = ADDR_TM_2D_TILED_THIN1;
      }
   } else if (tileMode == ADDR_TM_3D_TILED_THICK) {
      if (numSlices < 4) {
         tileMode = ADDR_TM_3D_TILED_THIN1;
      }
   }

   return ComputeSurfaceMipLevelTileMode(tileMode,
                                         bpp,
                                         level,
                                         width,
                                         height,
                                         numSlices,
                                         numSamples,
                                         isDepth,
                                         true);
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceAlignmentsLinear
*
*   @brief
*       Compute alignment for a linear tiled surface.
*
*   @return
*       TRUE if valid surface settings.
***************************************************************************************************
*/
bool
R600AddrLib::ComputeSurfaceAlignmentsLinear(AddrTileMode tileMode,
                                            uint32_t bpp,
                                            ADDR_SURFACE_FLAGS flags,
                                            uint32_t *pBaseAlign,
                                            uint32_t *pPitchAlign,
                                            uint32_t *pHeightAlign) const
{
   bool valid = true;

   switch (tileMode) {
   case ADDR_TM_LINEAR_GENERAL:
      *pBaseAlign = 1;
      *pPitchAlign = (bpp != 1) ? 1 : 8;
      *pHeightAlign = 1;
      break;
   case ADDR_TM_LINEAR_ALIGNED:
      *pBaseAlign = mPipeInterleaveBytes;
      *pPitchAlign = std::max<uint32_t>(64u, (8 * mPipeInterleaveBytes) / bpp);
      *pHeightAlign = 1;
      break;
   default:
      *pBaseAlign = 1;
      *pPitchAlign = 1;
      *pHeightAlign = 1;
      break;
   }

   AdjustPitchAlignment(flags, pPitchAlign);
   return valid;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceAlignmentsMicrotiled
*
*   @brief
*       Compute alignment for a micro tiled surface.
*
*   @return
*       TRUE if valid surface settings.
***************************************************************************************************
*/
bool
R600AddrLib::ComputeSurfaceAlignmentsMicrotiled(AddrTileMode tileMode,
                                                uint32_t bpp,
                                                ADDR_SURFACE_FLAGS flags,
                                                uint32_t numSamples,
                                                uint32_t *pBaseAlign,
                                                uint32_t *pPitchAlign,
                                                uint32_t *pHeightAlign) const
{
   if (bpp == 96 || bpp == 48 || bpp == 24) {
      bpp /= 3u;
   }

   auto microTileThickness = ComputeSurfaceThickness(tileMode);
   auto pitchAlignment = mPipeInterleaveBytes / bpp / numSamples / microTileThickness;

   *pBaseAlign = mPipeInterleaveBytes;
   *pPitchAlign = std::max<uint32_t>(8u, pitchAlignment);
   *pHeightAlign = 8;

   AdjustPitchAlignment(flags, pPitchAlign);
   return true;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeMacroTileAspectRatio
*
*   @brief
*       Compute aspect ratio for a tileMode
*
*   @return
*       Aspect ratio for tileMode
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeMacroTileAspectRatio(AddrTileMode tileMode) const
{
   switch (tileMode) {
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_3D_TILED_THIN1:
   case ADDR_TM_3B_TILED_THIN1:
      return 1;
   case ADDR_TM_2D_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN2:
      return 2;
   case ADDR_TM_2D_TILED_THIN4:
   case ADDR_TM_2B_TILED_THIN4:
      return 4;
   default:
      return 1;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::IsDualBaseAlignNeeded
*
*   @brief
*       Check if a tileMode requires dual base alignment
*
*   @return
*       TRUE if tileMode requres dual base alignment
***************************************************************************************************
*/
bool
R600AddrLib::IsDualBaseAlignNeeded(AddrTileMode tileMode) const
{
   if (mChipFamily == ADDR_CHIP_FAMILY_R6XX) {
      if (tileMode > ADDR_TM_1D_TILED_THICK) {
         return true;
      }
   }

   return false;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceAlignmentsMacrotiled
*
*   @brief
*       Compute alignment for a macro tiled surface.
*
*   @return
*       TRUE if valid surface settings.
***************************************************************************************************
*/
bool
R600AddrLib::ComputeSurfaceAlignmentsMacrotiled(AddrTileMode tileMode,
                                                uint32_t bpp,
                                                ADDR_SURFACE_FLAGS flags,
                                                uint32_t numSamples,
                                                uint32_t *pBaseAlign,
                                                uint32_t *pPitchAlign,
                                                uint32_t *pHeightAlign,
                                                uint32_t *pMacroWidth,
                                                uint32_t *pMacroHeight) const
{
   auto aspectRatio = ComputeMacroTileAspectRatio(tileMode);
   auto thickness = ComputeSurfaceThickness(tileMode);

   if (bpp == 96 || bpp == 48 || bpp == 24) {
      bpp /= 3u;
   }

   if (bpp == 3) {
      bpp = 1;
   }

   auto numBanks = mBanks;
   auto numPipes = mPipes;
   auto groupBytes = mPipeInterleaveBytes;
   auto splitBytes = mSplitSize;
   auto baseAlign = uint32_t { 0 };

   auto macroTileWidth = 8 * numBanks / aspectRatio;
   auto macroTileHeight = aspectRatio * 8 * numPipes;
   auto pitchAlign = std::max<uint32_t>(macroTileWidth, macroTileWidth * (groupBytes / bpp / (8 * thickness) / numSamples));

   auto heightAlign = macroTileHeight;
   auto macroTileBytes = numSamples * BITS_TO_BYTES(bpp * macroTileHeight * macroTileWidth);

   if (mChipFamily == ADDR_CHIP_FAMILY_R6XX && numSamples == 1) {
      macroTileBytes *= 2;
   }

   if (thickness == 1) {
      baseAlign = std::max<uint32_t>(macroTileBytes, BITS_TO_BYTES(numSamples * heightAlign * bpp * pitchAlign));
   } else {
      baseAlign = std::max<uint32_t>(groupBytes, BITS_TO_BYTES(4 * heightAlign * bpp * pitchAlign));
   }

   auto microTileBytes = BITS_TO_BYTES(thickness * numSamples * bpp * 64);
   auto numSlicesPerMicroTile = 1;

   if (microTileBytes >= splitBytes) {
      numSlicesPerMicroTile = microTileBytes / splitBytes;
   }

   baseAlign /= numSlicesPerMicroTile;

   if (IsDualBaseAlignNeeded(tileMode)) {
      auto macroBytes = BITS_TO_BYTES(bpp * macroTileHeight * macroTileWidth);

      if (baseAlign / macroBytes % 2) {
         baseAlign += macroBytes;
      }
   }

   *pBaseAlign = baseAlign;
   *pHeightAlign = heightAlign;
   *pMacroWidth = macroTileWidth;
   *pMacroHeight = macroTileHeight;
   *pPitchAlign = pitchAlign;
   return true;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceInfoLinear
*
*   @brief
*       Compute linear surface sizes include padded pitch, height, slices, total size in
*       bytes, meanwhile alignments as well. Since it is linear mode, so output tile mode
*       will not be changed here. Results are returned through output parameters.
*
*   @return
*       TRUE if no error occurs
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::ComputeSurfaceInfoLinear(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                      ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                                      uint32_t padDims,
                                      AddrTileMode tileMode) const
{
   auto microTileThickness = ComputeSurfaceThickness(tileMode);
   auto pitch = pIn->width;
   auto height = pIn->height;
   auto numSlices = pIn->numSlices;
   auto numSamples = pIn->numSamples;
   auto mipLevel = pIn->mipLevel;
   auto bpp = pIn->bpp;

   ComputeSurfaceAlignmentsLinear(pIn->tileMode,
                                  pIn->bpp,
                                  pIn->flags,
                                  &pOut->baseAlign,
                                  &pOut->pitchAlign,
                                  &pOut->heightAlign);

   if (pIn->flags.linearWA && mipLevel == 0) {
      pitch = NextPow2(pitch / 3);
   }

   if (mipLevel) {
      pitch = NextPow2(pitch);
      height = NextPow2(height);

      if (pIn->flags.cube) {
         if (numSlices <= 1) {
            padDims = 2;
         } else {
            padDims = 0;
         }
      } else {
         numSlices = NextPow2(numSlices);
      }
   }

   PadDimensions(tileMode,
                 pIn->flags,
                 padDims,
                 &pitch,
                 pOut->pitchAlign,
                 &height,
                 pOut->heightAlign,
                 &numSlices,
                 microTileThickness);

   if (pIn->flags.linearWA && mipLevel == 0) {
      pitch *= 3;
   }

   auto slices = (numSlices * numSamples) / microTileThickness;
   auto surfaceSize = BITS_TO_BYTES(static_cast<uint64_t>(height) * pitch * slices * bpp * numSamples);

   pOut->pitch = pitch;
   pOut->height = height;
   pOut->depth = numSlices;
   pOut->surfSize = surfaceSize;
   pOut->depthAlign = microTileThickness;
   pOut->tileMode = tileMode;
   return ADDR_OK;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceInfoMicroTiled
*
*   @brief
*       Compute 1D/Micro Tiled surface sizes include padded pitch, height, slices, total
*       size in bytes, meanwhile alignments as well. Results are returned through output
*       parameters.
*
*   @return
*       TRUE if no error occurs
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::ComputeSurfaceInfoMicroTiled(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                          ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                                          uint32_t padDims,
                                          AddrTileMode tileMode) const
{
   auto microTileThickness = ComputeSurfaceThickness(tileMode);
   auto pitch = pIn->width;
   auto height = pIn->height;
   auto numSlices = pIn->numSlices;
   auto numSamples = pIn->numSamples;
   auto mipLevel = pIn->mipLevel;
   auto bpp = pIn->bpp;

   if (mipLevel) {
      pitch = NextPow2(pitch);
      height = NextPow2(height);

      if (pIn->flags.cube) {
         if (numSlices <= 1) {
            padDims = 2;
         } else {
            padDims = 0;
         }
      } else {
         numSlices = NextPow2(numSlices);
      }

      if (tileMode == ADDR_TM_1D_TILED_THICK && numSlices < 4) {
         tileMode = ADDR_TM_1D_TILED_THIN1;
         microTileThickness = 1;
      }
   }

   ComputeSurfaceAlignmentsMicrotiled(tileMode,
                                      pIn->bpp,
                                      pIn->flags,
                                      pIn->numSamples,
                                      &pOut->baseAlign,
                                      &pOut->pitchAlign,
                                      &pOut->heightAlign);

   PadDimensions(tileMode,
                 pIn->flags,
                 padDims,
                 &pitch,
                 pOut->pitchAlign,
                 &height,
                 pOut->heightAlign,
                 &numSlices,
                 microTileThickness);

   auto surfaceSize = BITS_TO_BYTES(static_cast<uint64_t>(height) * pitch * numSlices * bpp * numSamples);

   pOut->pitch = pitch;
   pOut->height = height;
   pOut->depth = numSlices;
   pOut->surfSize = surfaceSize;
   pOut->tileMode = tileMode;
   pOut->depthAlign = microTileThickness;
   return ADDR_OK;
}


/**
***************************************************************************************************
*   R600AddrLib::IsThickMacroTiled
*
*   @brief
*       Check if a tileMode is thick macro tiled
*
*   @return
*       TRUE if thick macro tiled
***************************************************************************************************
*/
bool
R600AddrLib::IsThickMacroTiled(AddrTileMode tileMode) const
{
   switch (tileMode) {
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THICK:
      return true;
   default:
      return false;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::IsBankSwappedTileMode
*
*   @brief
*       Check if a tileMode is a bank swapped tile mode
*
*   @return
*       TRUE if bank swapped tile mode
***************************************************************************************************
*/
bool
R600AddrLib::IsBankSwappedTileMode(AddrTileMode tileMode) const
{
   switch (tileMode) {
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_2B_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN4:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3B_TILED_THIN1:
   case ADDR_TM_3B_TILED_THICK:
      return true;
   default:
      return false;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::IsDualPitchAlignNeeded
*
*   @brief
*       Check if a tileMode requires dual pitch align
*
*   @return
*       TRUE if requires dual pitch align
***************************************************************************************************
*/
bool
R600AddrLib::IsDualPitchAlignNeeded(AddrTileMode tileMode,
                                    bool isDepth,
                                    uint32_t mipLevel) const
{
   if (isDepth || mipLevel != 0 || mChipFamily != ADDR_CHIP_FAMILY_R6XX) {
      return false;
   }

   switch (tileMode) {
   case ADDR_TM_LINEAR_GENERAL:
   case ADDR_TM_LINEAR_ALIGNED:
   case ADDR_TM_1D_TILED_THIN1:
   case ADDR_TM_1D_TILED_THICK:
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THICK:
      return false;
   default:
      return true;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceBankSwappedWidth
*
*   @brief
*       Computes the bank swapped width of a surface.
*
*   @return
*       Bank swap width
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeSurfaceBankSwappedWidth(AddrTileMode tileMode,
                                            uint32_t bpp,
                                            uint32_t numSamples,
                                            uint32_t pitch,
                                            uint32_t *pSlicesPerTile) const
{
   auto bankSwapWidth = uint32_t { 0 };
   auto numBanks = mBanks;
   auto numPipes = mPipes;
   auto swapSize = mSwapSize;
   auto rowSize = mRowSize;
   auto splitSize = mSplitSize;
   auto groupSize = mPipeInterleaveBytes;
   auto slicesPerTile = uint32_t { 1 };
   auto bytesPerSample = 8 * bpp;
   auto samplesPerTile = splitSize / bytesPerSample;

   if (splitSize / bytesPerSample) {
      slicesPerTile = std::max<uint32_t>(1u, numSamples / samplesPerTile);
   }

   if (pSlicesPerTile) {
      *pSlicesPerTile = slicesPerTile;
   }

   if (IsThickMacroTiled(tileMode)) {
      numSamples = 4;
   }

   auto bytesPerTileSlice = numSamples * bytesPerSample / slicesPerTile;

   if (IsBankSwappedTileMode(tileMode)) {
      auto factor = ComputeMacroTileAspectRatio(tileMode);
      auto swapTiles = std::max<uint32_t>(1u, (swapSize >> 1) / bpp);
      auto swapWidth = swapTiles * 8 * numBanks;
      auto heightBytes = numSamples * factor * numPipes * bpp / slicesPerTile;
      auto swapMax = numPipes * numBanks * rowSize / heightBytes;
      auto swapMin = groupSize * 8 * numBanks / bytesPerTileSlice;

      bankSwapWidth = std::min(swapMax, std::max(swapMin, swapWidth));

      while (bankSwapWidth >= 2 * pitch) {
         bankSwapWidth >>= 1;
      }
   }

   return bankSwapWidth;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceInfoMacroTiled
*
*   @brief
*       Compute 2D/macro tiled surface sizes include padded pitch, height, slices, total
*       size in bytes, meanwhile output suitable tile mode and alignments might be changed
*       in this call as well. Results are returned through output parameters.
*
*   @return
*       ADDR_OK if no error occurs
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::ComputeSurfaceInfoMacroTiled(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                          ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut,
                                          uint32_t padDims,
                                          AddrTileMode tileMode,
                                          AddrTileMode baseTileMode) const
{
   auto macroWidth = uint32_t { 0 };
   auto macroHeight = uint32_t { 0 };
   auto microTileThickness = ComputeSurfaceThickness(tileMode);
   auto pitch = pIn->width;
   auto height = pIn->height;
   auto numSlices = pIn->numSlices;
   auto numSamples = pIn->numSamples;
   auto mipLevel = pIn->mipLevel;
   auto bpp = pIn->bpp;
   auto pitchAlign = pOut->pitchAlign;

   if (mipLevel) {
      pitch = NextPow2(pitch);
      height = NextPow2(height);

      if (pIn->flags.cube) {
         if (numSlices <= 1) {
            padDims = 2;
         } else {
            padDims = 0;
         }
      } else {
         numSlices = NextPow2(numSlices);
      }

      if (tileMode == ADDR_TM_2D_TILED_THICK && numSlices < 4) {
         tileMode = ADDR_TM_2D_TILED_THIN1;
         microTileThickness = 1;
      }
   }

   if (tileMode != baseTileMode && mipLevel != 0 && IsThickMacroTiled(baseTileMode) && !IsThickMacroTiled(tileMode)) {
      ComputeSurfaceAlignmentsMacrotiled(baseTileMode,
                                         pIn->bpp,
                                         pIn->flags,
                                         pIn->numSamples,
                                         &pOut->baseAlign,
                                         &pOut->pitchAlign,
                                         &pOut->heightAlign,
                                         &macroWidth,
                                         &macroHeight);

      auto pitchAlignFactor = std::max<uint32_t>(1, (mPipeInterleaveBytes >> 3) / bpp);

      if (pitch < (pOut->pitchAlign * pitchAlignFactor) || height < pOut->heightAlign) {
         return ComputeSurfaceInfoMicroTiled(pIn, pOut, padDims, ADDR_TM_1D_TILED_THIN1);
      }
   }

   ComputeSurfaceAlignmentsMacrotiled(tileMode,
                                      pIn->bpp,
                                      pIn->flags,
                                      pIn->numSamples,
                                      &pOut->baseAlign,
                                      &pitchAlign,
                                      &pOut->heightAlign,
                                      &macroWidth,
                                      &macroHeight);

   auto bankSwappedWidth = ComputeSurfaceBankSwappedWidth(tileMode, bpp, numSamples, pitch, nullptr);
   pitchAlign = std::max(pitchAlign, bankSwappedWidth);

   if (IsDualPitchAlignNeeded(tileMode, pIn->flags.depth, mipLevel)) {
      auto tilePerGroup = (mPipeInterleaveBytes >> 3) / bpp / numSamples;
      tilePerGroup = std::max<uint32_t>(1u, tilePerGroup / ComputeSurfaceThickness(tileMode));

      auto evenWidth = ((pitch - 1) / macroWidth) & 1;
      auto evenHeight = ((height - 1) / macroHeight) & 1;

      if (numSamples == 1 && tilePerGroup == 1 && !evenWidth) {
         if (pitch > macroWidth || (!evenHeight && height > macroHeight)) {
            pitch += macroWidth;
         }
      }
   }

   PadDimensions(tileMode,
                 pIn->flags,
                 padDims,
                 &pitch,
                 pitchAlign,
                 &height,
                 pOut->heightAlign,
                 &numSlices,
                 microTileThickness);

   auto surfaceSize = BITS_TO_BYTES(static_cast<uint64_t>(height) * pitch * numSlices * bpp * numSamples);

   pOut->pitch = pitch;
   pOut->height = height;
   pOut->depth = numSlices;
   pOut->surfSize = surfaceSize;
   pOut->tileMode = tileMode;
   pOut->pitchAlign = pitchAlign;
   pOut->depthAlign = microTileThickness;
   return ADDR_OK;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeSurfaceInfo
*
*   @brief
*       Entry of R600AddrLib ComputeSurfaceInfo
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::HwlComputeSurfaceInfo(const ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                                   ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE result;
   auto numSamples = std::max(1u, pIn->numSamples);
   auto tileMode = pIn->tileMode;
   auto padDims = 0u;

   if (pIn->flags.cube && pIn->mipLevel == 0) {
      padDims = 2;
   }

   if (pIn->flags.fmask) {
      tileMode = ConvertToNonBankSwappedMode(tileMode);
   } else {
      tileMode = ComputeSurfaceMipLevelTileMode(tileMode,
                                                pIn->bpp,
                                                pIn->mipLevel,
                                                pIn->width,
                                                pIn->height,
                                                pIn->numSlices,
                                                numSamples,
                                                pIn->flags.depth,
                                                0);
   }

   switch (tileMode) {
   case ADDR_TM_LINEAR_GENERAL:
   case ADDR_TM_LINEAR_ALIGNED:
      result = ComputeSurfaceInfoLinear(pIn, pOut, padDims, tileMode);
      break;
   case ADDR_TM_1D_TILED_THIN1:
   case ADDR_TM_1D_TILED_THICK:
      result = ComputeSurfaceInfoMicroTiled(pIn, pOut, padDims, tileMode);
      break;
   case ADDR_TM_2D_TILED_THIN1:
   case ADDR_TM_2D_TILED_THIN2:
   case ADDR_TM_2D_TILED_THIN4:
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_2B_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN4:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3D_TILED_THIN1:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THIN1:
   case ADDR_TM_3B_TILED_THICK:
      result = ComputeSurfaceInfoMacroTiled(pIn, pOut, padDims, tileMode, pIn->tileMode);
      break;
   default:
      result = ADDR_INVALIDPARAMS;
   }

   return result;
}


/**
***************************************************************************************************
*   R600AddrLib::GetTileType
*
*   @brief
*       Returns appropriate tile type for depth / non depth surfaces
*
*   @return
*       AddrTileType
***************************************************************************************************
*/
AddrTileType
R600AddrLib::GetTileType(bool isDepth) const
{
   if (isDepth) {
      return ADDR_NON_DISPLAYABLE;
   } else {
      return ADDR_DISPLAYABLE;
   }
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceAddrFromCoordMicroTiled
*
*   @brief
*       Computes the surface address and bit position from a
*       coordinate for 2D tilied (macro tiled)
*
*   @return
*       The byte address
***************************************************************************************************
*/
uint64_t
R600AddrLib::ComputeSurfaceAddrFromCoordMicroTiled(uint32_t x,
                                                   uint32_t y,
                                                   uint32_t slice,
                                                   uint32_t bpp,
                                                   uint32_t pitch,
                                                   uint32_t height,
                                                   AddrTileMode tileMode,
                                                   bool isDepth,
                                                   uint32_t tileBase,
                                                   uint32_t compBits,
                                                   uint32_t *pBitPosition) const
{
   uint64_t microTileThickness = 1u;

   if (tileMode == ADDR_TM_1D_TILED_THICK) {
      microTileThickness = 4u;
   }

   uint64_t microTileBytes = BITS_TO_BYTES(MicroTilePixels * microTileThickness * bpp);
   uint64_t microTilesPerRow = pitch / MicroTileWidth;
   uint64_t microTileIndexX = x / MicroTileWidth;
   uint64_t microTileIndexY = y / MicroTileHeight;
   uint64_t microTileIndexZ = slice / microTileThickness;

   uint64_t microTileOffset = microTileBytes * (microTileIndexX + microTileIndexY * microTilesPerRow);

   uint64_t sliceBytes = BITS_TO_BYTES(pitch * height * microTileThickness * bpp);
   uint64_t sliceOffset = microTileIndexZ * sliceBytes;

   uint64_t pixelIndex = ComputePixelIndexWithinMicroTile(x, y, slice, bpp, tileMode, GetTileType(isDepth));
   uint64_t pixelOffset;

   if (compBits && compBits != bpp && isDepth) {
      pixelOffset = tileBase + compBits * pixelIndex;
   } else {
      pixelOffset = bpp * pixelIndex;
   }

   *pBitPosition = pixelOffset % 8;
   pixelOffset /= 8;

   return pixelOffset + microTileOffset + sliceOffset;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputePipeFromCoordWoRotation
*
*   @brief
*       Computes the pipe index from coord
*
*   @return
*       The pipe index
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputePipeFromCoordWoRotation(uint32_t x, uint32_t y) const
{
   uint32_t pipe;
   uint32_t pipeBit0 = 0;
   uint32_t pipeBit1 = 0;
   uint32_t pipeBit2 = 0;
   uint32_t pipeBit3 = 0;

   uint32_t x3 = _BIT(x, 3);
   uint32_t x4 = _BIT(x, 4);
   uint32_t x5 = _BIT(x, 5);
   uint32_t y3 = _BIT(y, 3);
   uint32_t y4 = _BIT(y, 4);
   uint32_t y5 = _BIT(y, 5);

   switch (mPipes) {
   case 1:
      pipeBit0 = 0;
      break;
   case 2:
      pipeBit0 = (y3 ^ x3);
      break;
   case 4:
      pipeBit0 = (y3 ^ x4);
      pipeBit1 = (y4 ^ x3);
      break;
   case 8:
      pipeBit0 = (y3 ^ x5);
      pipeBit1 = (y4 ^ x5 ^ x4);
      pipeBit2 = (y5 ^ x3);
      break;
   default:
      pipe = 0;
      break;
   }

   pipe = pipeBit0 | (pipeBit1 << 1) | (pipeBit2 << 2);
   return pipe;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputePipeFromCoordWoRotation
*
*   @brief
*       Computes the bank index from coord
*
*   @return
*       The bank index
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeBankFromCoordWoRotation(uint32_t x, uint32_t y) const
{
   uint32_t numPipes = mPipes;
   uint32_t numBanks = mBanks;
   uint32_t bankOpt = mConfigFlags.optimalBankSwap;

   uint32_t tx = x / numBanks;
   uint32_t ty = y / numPipes;

   uint32_t bank;
   uint32_t bankBit0 = 0;
   uint32_t bankBit1 = 0;
   uint32_t bankBit2 = 0;

   uint32_t x3 = _BIT(x, 3);
   uint32_t x4 = _BIT(x, 4);
   uint32_t x5 = _BIT(x, 5);

   uint32_t tx3 = _BIT(tx, 3);

   uint32_t ty3 = _BIT(ty, 3);
   uint32_t ty4 = _BIT(ty, 4);
   uint32_t ty5 = _BIT(ty, 5);

   switch (mBanks)
   {
   case 4:
      bankBit0 = (ty4 ^ x3);

      if (bankOpt == 1 && numPipes == 8) {
         bankBit0 ^= x5;
      }

      bankBit1 = (ty3 ^ x4);
      break;
   case 8:
      bankBit0 = (ty5 ^ x3);

      if (bankOpt == 1 && numPipes == 8) {
         bankBit0 ^= tx3;
      }

      bankBit1 = (ty5 ^ ty4 ^ x4);
      bankBit2 = (ty3 ^ x5);
      break;
   }

   bank = bankBit0 | (bankBit1 << 1) | (bankBit2 << 2);
   return bank;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSurfaceAddrFromCoordMicroTiled
*
*   @brief
*       Computes the surface address and bit position from a
*       coordinate for 2D tilied (macro tiled)
*
*   @return
*       The byte address
***************************************************************************************************
*/
uint64_t
R600AddrLib::ComputeSurfaceAddrFromCoordMacroTiled(uint32_t x,
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
                                                   uint32_t *pBitPosition) const
{
   uint64_t numPipes = mPipes;
   uint64_t numBanks = mBanks;
   uint64_t numGroupBits = Log2(mPipeInterleaveBytes);
   uint64_t numPipeBits = Log2(mPipes);
   uint64_t numBankBits = Log2(mBanks);

   uint64_t microTileThickness = ComputeSurfaceThickness(tileMode);
   uint64_t microTileBits = MicroTilePixels * microTileThickness * bpp * numSamples;
   uint64_t microTileBytes = microTileBits / 8;

   uint64_t pixelIndex = ComputePixelIndexWithinMicroTile(x, y, slice, bpp, tileMode, GetTileType(isDepth));

   uint64_t sampleOffset;
   uint64_t pixelOffset;

   if (isDepth) {
      if (compBits && compBits != bpp) {
         sampleOffset = tileBase + compBits * sample;
         pixelOffset = numSamples * compBits * pixelIndex;
      } else {
         sampleOffset = bpp * sample;
         pixelOffset = numSamples * bpp * pixelIndex;
      }
   } else {
      sampleOffset = sample * (microTileBits / numSamples);
      pixelOffset = bpp * pixelIndex;
   }

   uint64_t elemOffset = pixelOffset + sampleOffset;
   *pBitPosition = static_cast<uint32_t>(elemOffset % 8);

   uint64_t bytesPerSample = microTileBytes / numSamples;
   uint64_t samplesPerSlice;
   uint64_t numSampleSplits;
   uint64_t sampleSlice;
   uint64_t tileSliceBits;

   if (numSamples > 1 && microTileBytes > static_cast<uint64_t>(mSplitSize)) {
      samplesPerSlice = mSplitSize / bytesPerSample;
      numSampleSplits = numSamples / samplesPerSlice;
      numSamples = static_cast<uint32_t>(samplesPerSlice);

      tileSliceBits = microTileBits / numSampleSplits;
      sampleSlice = elemOffset / tileSliceBits;
      elemOffset %= tileSliceBits;
   } else {
      samplesPerSlice = numSamples;
      numSampleSplits = 1;
      sampleSlice = 0;
   }

   elemOffset /= 8;

   uint64_t pipe = ComputePipeFromCoordWoRotation(x, y);
   uint64_t bank = ComputeBankFromCoordWoRotation(x, y);

   uint64_t bankPipe = pipe + numPipes * bank;
   uint64_t rotation = ComputeSurfaceRotationFromTileMode(tileMode);
   uint64_t swizzle = pipeSwizzle + numPipes * bankSwizzle;
   uint64_t sliceIn = slice;

   if (IsThickMacroTiled(tileMode)) {
      sliceIn /= ThickTileThickness;
   }

   bankPipe ^= numPipes * sampleSlice * ((numBanks >> 1) + 1) ^ (swizzle + sliceIn * rotation);
   bankPipe %= numPipes * numBanks;
   pipe = bankPipe % numPipes;
   bank = bankPipe / numPipes;

   uint64_t sliceBytes = BITS_TO_BYTES(pitch * height * microTileThickness * bpp * numSamples);
   uint64_t sliceOffset = sliceBytes * ((sampleSlice + numSampleSplits * slice) / microTileThickness);

   uint64_t macroTilePitch = 8 * numBanks;
   uint64_t macroTileHeight = 8 * numPipes;

   switch (tileMode) {
   case ADDR_TM_2D_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN2:
      macroTilePitch /= 2;
      macroTileHeight *= 2;
      break;
   case ADDR_TM_2D_TILED_THIN4:
   case ADDR_TM_2B_TILED_THIN4:
      macroTilePitch /= 4;
      macroTileHeight *= 4;
      break;
   }

   uint64_t macroTilesPerRow = pitch / macroTilePitch;
   uint64_t macroTileBytes = BITS_TO_BYTES(numSamples * microTileThickness * bpp * macroTileHeight * macroTilePitch);
   uint64_t macroTileIndexX = x / macroTilePitch;
   uint64_t macroTileIndexY = y / macroTileHeight;
   uint64_t macroTileOffset = macroTileBytes * (macroTileIndexX + macroTilesPerRow * macroTileIndexY);
   uint64_t bankSwapWidth = { 0 };
   uint64_t swapIndex = { 0 };

   // Do bank swapping if needed
   switch (tileMode) {
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_2B_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN4:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3B_TILED_THIN1:
   case ADDR_TM_3B_TILED_THICK:
      static const uint32_t bankSwapOrder[] = { 0, 1, 3, 2, 6, 7, 5, 4, 0, 0 };
      bankSwapWidth = ComputeSurfaceBankSwappedWidth(tileMode, bpp, numSamples, pitch, nullptr);
      swapIndex = macroTilePitch * macroTileIndexX / bankSwapWidth;
      bank ^= bankSwapOrder[swapIndex & (mBanks - 1)];
      break;
   }

   // Calculate final offset
   uint64_t group_mask = (1 << numGroupBits) - 1;
   uint64_t total_offset = elemOffset + ((macroTileOffset + sliceOffset) >> (numBankBits + numPipeBits));

   uint64_t offset_high = (total_offset & ~group_mask) << (numBankBits + numPipeBits);
   uint64_t offset_low = total_offset & group_mask;
   uint64_t bank_bits = bank << (numPipeBits + numGroupBits);
   uint64_t pipe_bits = pipe << numGroupBits;
   uint64_t offset = bank_bits | pipe_bits | offset_low | offset_high;

   return offset;
}


/**
***************************************************************************************************
*   R600AddrLib::DispatchComputeSurfaceAddrFromCoord
*
*   @brief
*       Compute surface address from given coord (x, y, slice,sample)
*
*   @return
*       Address in bytes
***************************************************************************************************
*/
uint64_t
R600AddrLib::DispatchComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                                 ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const
{
   auto numSamples = std::max<uint32_t>(1u, pIn->numSamples);
   auto addr = uint64_t { 0 };

   switch (pIn->tileMode) {
   case ADDR_TM_LINEAR_GENERAL:
   case ADDR_TM_LINEAR_ALIGNED:
      addr = ComputeSurfaceAddrFromCoordLinear(pIn->x,
                                               pIn->y,
                                               pIn->slice,
                                               pIn->sample,
                                               pIn->bpp,
                                               pIn->pitch,
                                               pIn->height,
                                               pIn->numSlices,
                                               &pOut->bitPosition);
      break;
   case ADDR_TM_1D_TILED_THIN1:
   case ADDR_TM_1D_TILED_THICK:
      addr = ComputeSurfaceAddrFromCoordMicroTiled(pIn->x,
                                                   pIn->y,
                                                   pIn->slice,
                                                   pIn->bpp,
                                                   pIn->pitch,
                                                   pIn->height,
                                                   pIn->tileMode,
                                                   pIn->isDepth,
                                                   pIn->tileBase,
                                                   pIn->compBits,
                                                   &pOut->bitPosition);
      break;
   case ADDR_TM_2D_TILED_THIN1:
   case ADDR_TM_2D_TILED_THIN2:
   case ADDR_TM_2D_TILED_THIN4:
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THIN1:
   case ADDR_TM_2B_TILED_THIN2:
   case ADDR_TM_2B_TILED_THIN4:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3D_TILED_THIN1:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THIN1:
   case ADDR_TM_3B_TILED_THICK:
      addr = ComputeSurfaceAddrFromCoordMacroTiled(pIn->x,
                                                   pIn->y,
                                                   pIn->slice,
                                                   pIn->sample,
                                                   pIn->bpp,
                                                   pIn->pitch,
                                                   pIn->height,
                                                   numSamples,
                                                   pIn->tileMode,
                                                   pIn->isDepth,
                                                   pIn->tileBase,
                                                   pIn->compBits,
                                                   pIn->pipeSwizzle,
                                                   pIn->bankSwizzle,
                                                   &pOut->bitPosition);
      break;
   default:
      addr = 0;
   }

   return addr;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeSurfaceAddrFromCoord
*   @brief
*       Entry of R600AddrLib ComputeSurfaceAddrFromCoord
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::HwlComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                            ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;

   if (pIn->pipeSwizzle >= mPipes
    || pIn->bankSwizzle >= mBanks
    || pIn->x > pIn->pitch
    || pIn->y > pIn->height
    || pIn->numSamples > 8) {
      returnCode = ADDR_INVALIDPARAMS;
   } else {
      pOut->addr = DispatchComputeSurfaceAddrFromCoord(pIn, pOut);
   }

   return returnCode;
}


/**
***************************************************************************************************
*   R600AddrLib::ExtractBankPipeSwizzle
*
*   @brief
*       Extract bank/pipe swizzle from base256b
*
*   @return
*       N/A
***************************************************************************************************
*/
void
R600AddrLib::ExtractBankPipeSwizzle(uint32_t base256b,
                                    uint32_t *pBankSwizzle,
                                    uint32_t *pPipeSwizzle) const
{
   auto bankMask = (1 << Log2(mBanks)) - 1;
   auto pipeMask = (1 << Log2(mPipes)) - 1;
   auto groupBytes = mPipeInterleaveBytes;

   *pPipeSwizzle = (base256b / (groupBytes >> 8)) & pipeMask;
   *pBankSwizzle = (base256b / (groupBytes >> 8) / mPipes) & bankMask;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlExtractBankPipeSwizzle
*
*   @brief
*       Entry of R600AddrLib ExtractBankPipeSwizzle
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::HwlExtractBankPipeSwizzle(const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn,
                                       ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut) const
{
   ExtractBankPipeSwizzle(pIn->base256b,
                          &pOut->bankSwizzle,
                          &pOut->pipeSwizzle);

   return ADDR_OK;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeHtileBpp
*
*   @brief
*       Compute htile bpp
*
*   @return
*       Htile bpp
***************************************************************************************************
*/
uint32_t
R600AddrLib::HwlComputeHtileBpp(bool isWidth8,
                                bool isHeight8) const
{
   auto htileX = isWidth8 ? 1 : 2;
   auto htileY = isHeight8 ? 1 : 2;

   return htileX * htileY * 32;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeHtileBaseAlign
*
*   @brief
*       Compute htile base alignment
*
*   @return
*       Htile base alignment
***************************************************************************************************
*/
uint32_t
R600AddrLib::HwlComputeHtileBaseAlign(bool isLinear,
                                      uint32_t pipes) const
{
   auto align = pipes * mPipeInterleaveBytes;

   if (isLinear) {
      align = std::max(align, BITS_TO_BYTES(pipes * HtileCacheBits));
   }

   return align;
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeHtileBytes
*
*   @brief
*       Compute htile size in bytes
*
*   @return
*       Htile size in bytes
***************************************************************************************************
*/
uint64_t
R600AddrLib::HwlComputeHtileBytes(uint32_t pitch,
                                  uint32_t height,
                                  uint32_t bpp,
                                  bool isLinear,
                                  uint32_t numSlices,
                                  uint32_t baseAlign) const
{
   auto htileCacheLineSize = BITS_TO_BYTES(HtileCacheBits);
   auto sliceBytes = BITS_TO_BYTES(static_cast<uint64_t>(height) * pitch * bpp * numSlices / 0x40);
   auto htileBytes = PowTwoAlign<uint64_t>(sliceBytes, mPipes * htileCacheLineSize);

   if (isLinear) {
      htileBytes = PowTwoAlign<uint64_t>(htileBytes, baseAlign);
   }

   return htileBytes;
}


/**
***************************************************************************************************
*   R600AddrLib::ComputeSliceTileSwizzle
*
*   @brief
*       Compute cubemap/3d texture faces/slices tile swizzle
*
*   @return
*       Tile swizzle
***************************************************************************************************
*/
uint32_t
R600AddrLib::ComputeSliceTileSwizzle(AddrTileMode tileMode,
                                     uint32_t baseSwizzle,
                                     uint32_t slice,
                                     size_t baseAddr) const
{
   if (!IsMacroTiled(tileMode)) {
      return 0;
   }

   auto thickness = ComputeSurfaceThickness(tileMode);
   auto rotation = ComputeSurfaceRotationFromTileMode(tileMode);
   auto groupMask = (mPipes * mBanks) - 1;

   auto firstSlice = slice / thickness;
   auto tileSwizzle = (baseSwizzle + firstSlice * rotation) & groupMask;

   baseAddr ^= tileSwizzle * mPipeInterleaveBytes;
   baseAddr >>= 8;

   return static_cast<uint32_t>(baseAddr);
}


/**
***************************************************************************************************
*   R600AddrLib::HwlComputeSliceTileSwizzle
*
*   @brief
*       Entry of R600AddrLib ComputeSliceTileSwizzle
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
R600AddrLib::HwlComputeSliceTileSwizzle(const ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn,
                                        ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut) const
{
   pOut->tileSwizzle = ComputeSliceTileSwizzle(pIn->tileMode,
                                               pIn->baseSwizzle,
                                               pIn->slice,
                                               pIn->baseAddr);

   return ADDR_OK;
}
