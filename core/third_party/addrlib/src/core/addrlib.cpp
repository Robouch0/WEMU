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
* @file  addrlib.cpp
* @brief Contains the implementation for the AddrLib base class..
***************************************************************************************************
*/

#include <algorithm>
#include <cstring>
#include "addrlib.h"


/**
***************************************************************************************************
*   AddrLib::AddrLib
*
*   @brief
*       Constructor for the AddrLib class with hClient as parameter
*
***************************************************************************************************
*/
AddrLib::AddrLib(ADDR_CLIENT_HANDLE hClient) :
   AddrObject(hClient),
   mClass(BASE_ADDRLIB),
   mChipFamily(ADDR_CHIP_FAMILY_IVLD),
   mChipRevision(0),
   mVersion(ADDRLIB_VERSION),
   mElemLib(nullptr),
   mPipes(0),
   mBanks(0),
   mPipeInterleaveBytes(0),
   mRowSize(0)
{
   mConfigFlags.value = 0;
}


/**
***************************************************************************************************
*   AddrLib::Create
*
*   @brief
*       Creates and initializes AddrLib object.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::Create(const ADDR_CREATE_INPUT *pCreateIn, ADDR_CREATE_OUTPUT *pCreateOut)
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;
   AddrLib *pLib = nullptr;

   if (pCreateIn->createFlags.fillSizeFields) {
      if ((pCreateIn->size != sizeof(ADDR_CREATE_INPUT)) ||
          (pCreateOut->size != sizeof(ADDR_CREATE_OUTPUT))) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (returnCode == ADDR_OK) {
      if (pCreateIn->callbacks.allocSysMem && pCreateIn->callbacks.freeSysMem) {
         AddrObject::SetupSysMemFuncs(pCreateIn->callbacks.allocSysMem, pCreateIn->callbacks.freeSysMem);

         switch (pCreateIn->chipEngine) {
         case CIASICIDGFXENGINE_R600:
            pLib = AddrR600HwlInit(pCreateIn->hClient);
            break;
         default:
            pLib = nullptr;
         }
      }
   }

   if (pLib) {
      pLib->mDebugPrint = pCreateIn->callbacks.debugPrint;
      pLib->mConfigFlags.forceLinearAligned = pCreateIn->createFlags.forceLinearAligned;
      pLib->mConfigFlags.noCubeMipSlicesPad = pCreateIn->createFlags.noCubeMipSlicesPad;
      pLib->mConfigFlags.sliceSizeComputing = pCreateIn->createFlags.sliceSizeComputing;
      pLib->mConfigFlags.fillSizeFields = pCreateIn->createFlags.fillSizeFields;
      pLib->mConfigFlags.useTileIndex = pCreateIn->createFlags.useTileIndex;
      pLib->mConfigFlags.useTileCaps = pCreateIn->createFlags.useTileCaps;
      pLib->SetAddrChipFamily(pCreateIn->chipFamily, pCreateIn->chipRevision);

      if (pLib->HwlInitGlobalParams(pCreateIn)) {
         pLib->mElemLib = AddrElemLib::Create(pLib, pCreateIn);
      } else {
         pLib->mElemLib = nullptr;
      }

      if (pLib->mElemLib) {
         pLib->mElemLib->SetConfigFlags(pLib->mConfigFlags);
      } else {
         pLib->Destroy();
         pLib = nullptr;
      }
   }

   pCreateOut->hLib = pLib;

   if (!pLib) {
      returnCode = ADDR_ERROR;
   }

   return returnCode;
}

void
AddrLib::Destroy()
{
   auto client = mClient;
   this->~AddrLib();
   AddrObject::ClientFree(this, client);
}


/**
***************************************************************************************************
*   AddrLib::GetAddrLib
*
*   @brief
*      Get AddrLib pointer
*
*   @return
*      An AddrLib class pointer
***************************************************************************************************
*/
AddrLib *
AddrLib::GetAddrLib(ADDR_HANDLE hLib)
{
   return reinterpret_cast<AddrLib *>(hLib);
}


/**
***************************************************************************************************
*   AddrLib::GetAddrChipFamily
*
*   @brief
*      Returns asic chip family name defined by AddrLib
*
*   @return
*      AddrChipFamily
***************************************************************************************************
*/
AddrChipFamily
AddrLib::GetAddrChipFamily()
{
   return mChipFamily;
}


/**
***************************************************************************************************
*   AddrLib::GetFillSizeFieldsFlags
*
*   @brief
*      Returns fillSizeFields flag
*
*   @return
*      True if fill size fields is required
***************************************************************************************************
*/
bool
AddrLib::GetFillSizeFieldsFlags() const
{
   return !!mConfigFlags.fillSizeFields;
}


/**
***************************************************************************************************
*   AddrLib::GetSliceComputingFlags
*
*   @brief
*      Returns sliceSizeComputing flag
*
*   @return
*      True if slice size computing enabled
***************************************************************************************************
*/
uint32_t
AddrLib::GetSliceComputingFlags() const
{
   return mConfigFlags.sliceSizeComputing;
}


/**
***************************************************************************************************
*   AddrLib::GetNumPipes
*
*   @brief
*      Returns number of pipes
*
*   @return
*      Number of pipes
***************************************************************************************************
*/
uint32_t
AddrLib::GetNumPipes(ADDR_TILEINFO *pTileInfo) const
{
   return mPipes;
}


/**
***************************************************************************************************
*   AddrLib::UseTileIndex
*
*   @brief
*      Returns tileIndex support
*
*   @return
*      true if tileIndex is supported
***************************************************************************************************
*/
bool
AddrLib::UseTileIndex(int32_t tileIndex) const
{
   return mConfigFlags.useTileIndex && (tileIndex != TILEINDEX_INVALID);
}


/**
***************************************************************************************************
*   AddrLib::SetAddrChipFamily
*
*   @brief
*       Convert familyID defined in atiid.h to AddrChipFamily and set mChipFamily/mChipRevision
*
*   @return
*      N/A
***************************************************************************************************
*/
void
AddrLib::SetAddrChipFamily(uint32_t uChipFamily, uint32_t uChipRevision)
{
   auto family = HwlConvertChipFamily(uChipFamily, uChipRevision);
   mChipFamily = family;
   mChipRevision = uChipRevision;
}


/**
***************************************************************************************************
*   AddrLib::IsMacroTiled
*
*   @brief
*       Check if the tile mode is macro tiled
*
*   @return
*       TRUE if it is macro tiled (2D/2B/3D/3B)
***************************************************************************************************
*/
bool
AddrLib::IsMacroTiled(AddrTileMode tileMode) const
{
   if (tileMode >= ADDR_TM_2D_TILED_THIN1 && tileMode <= ADDR_TM_3D_TILED_XTHICK) {
      return true;
   } else {
      return false;
   }
}


/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceThickness
*
*   @brief
*       Compute surface thickness
*
*   @return
*       Surface thickness
***************************************************************************************************
*/
uint32_t
AddrLib::ComputeSurfaceThickness(AddrTileMode tileMode) const
{
   switch (tileMode) {
   case ADDR_TM_1D_TILED_THICK:
   case ADDR_TM_2D_TILED_THICK:
   case ADDR_TM_2B_TILED_THICK:
   case ADDR_TM_3D_TILED_THICK:
   case ADDR_TM_3B_TILED_THICK:
      return 4u;
   case ADDR_TM_2D_TILED_XTHICK:
   case ADDR_TM_3D_TILED_XTHICK:
      return 8u;
   default:
      return 1u;
   }
}


/**
***************************************************************************************************
*   AddrLib::AdjustPitchAlignment
*
*   @brief
*       Adjusts pitch alignment for flipping surface
*
*   @return
*       N/A
*
***************************************************************************************************
*/
void
AddrLib::AdjustPitchAlignment(ADDR_SURFACE_FLAGS flags,
                              uint32_t *pPitchAlign) const
{
   if (flags.display) {
      *pPitchAlign = PowTwoAlign(*pPitchAlign, 32u);
   }
}


/**
***************************************************************************************************
*   AddrLib::ComputeMipLevel
*
*   @brief
*       Compute mipmap level width/height/slices
*   @return
*      N/A
***************************************************************************************************
*/
void
AddrLib::ComputeMipLevel(ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn) const
{
   if (mElemLib->IsBlockCompressed(pIn->format)) {
      if (pIn->mipLevel == 0 || pIn->flags.inputBaseMap) {
         pIn->width = PowTwoAlign(pIn->width, 4u);
         pIn->height = PowTwoAlign(pIn->height, 4u);
      }
   }

   if (!HwlComputeMipLevel(pIn) && pIn->mipLevel && pIn->flags.inputBaseMap) {
      auto width = pIn->width;
      auto height = pIn->height;
      auto slices = pIn->numSlices;

      width = std::max<uint32_t>(1u, width >> pIn->mipLevel);
      height = std::max<uint32_t>(1u, height >> pIn->mipLevel);

      if (!pIn->flags.cube) {
         slices = std::max<uint32_t>(1u, slices >> pIn->mipLevel);
      } else {
         slices = std::max<uint32_t>(1u, slices);
      }

      if (pIn->format != ADDR_FMT_32_32_32 && pIn->format != ADDR_FMT_32_32_32_FLOAT) {
         width = NextPow2(width);
         height = NextPow2(height);
         slices = NextPow2(slices);
      }

      pIn->width = width;
      pIn->height = height;
      pIn->numSlices = slices;
   }
}


/**
***************************************************************************************************
*   AddrLib::ComputeQbStereoInfo
*
*   @brief
*       Get quad buffer stereo information
*
*   @return
*       TRUE if no error
***************************************************************************************************
*/
bool
AddrLib::ComputeQbStereoInfo(ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const
{
   if (!pOut->pStereoInfo) {
      return false;
   }

   pOut->pStereoInfo->eyeHeight = pOut->height;
   pOut->pStereoInfo->rightOffset = static_cast<uint32_t>(pOut->surfSize);

   pOut->height <<= 1;
   pOut->pixelHeight <<= 1;
   pOut->surfSize <<= 1;
   return true;
}


/**
***************************************************************************************************
*   AddrLib::PadDimensions
*
*   @brief
*       Helper function to pad dimensions
*
*   @return
*       N/A
***************************************************************************************************
*/
uint32_t
AddrLib::PadDimensions(AddrTileMode tileMode,
                       ADDR_SURFACE_FLAGS flags,
                       uint32_t padDims,
                       uint32_t *pPitch,
                       uint32_t pitchAlign,
                       uint32_t *pHeight,
                       uint32_t heightAlign,
                       uint32_t *pSlices,
                       uint32_t sliceAlign) const
{
   auto thickness = ComputeSurfaceThickness(tileMode);

   if (padDims == 0) {
      padDims = 3;
   }

   if (IsPow2(pitchAlign)) {
      *pPitch = PowTwoAlign(*pPitch, pitchAlign);
   } else {
      *pPitch += pitchAlign - 1;
      *pPitch /= pitchAlign;
      *pPitch *= pitchAlign;
   }

   if (padDims > 1) {
      *pHeight = PowTwoAlign(*pHeight, heightAlign);
   }

   if (padDims > 2 || thickness > 1) {
      if (flags.cube && (!mConfigFlags.noCubeMipSlicesPad || flags.cubeAsArray)) {
         *pSlices = NextPow2(*pSlices);
      }

      if (thickness > 1) {
         *pSlices = PowTwoAlign(*pSlices, sliceAlign);
      }
   }

   return padDims;
}


/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::ComputeSurfaceInfo(ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn,
                            ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;
   AddrElemMode elemMode;

   if (GetFillSizeFieldsFlags()) {
      if (pIn->size != sizeof(ADDR_COMPUTE_SURFACE_INFO_INPUT) || pOut->size != sizeof(ADDR_COMPUTE_SURFACE_INFO_OUTPUT)) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (pIn->bpp > 128) {
      returnCode = ADDR_INVALIDPARAMS;
   }

   if (returnCode == ADDR_OK) {
      ADDR_TILEINFO tileInfoNull;

      ComputeMipLevel(pIn);

      auto width = pIn->width;
      auto height = pIn->height;
      auto bpp = pIn->bpp;
      auto expandX = uint32_t { 1 };
      auto expandY = uint32_t { 1 };
      auto sliceFlags = GetSliceComputingFlags();

      if (UseTileIndex(pIn->tileIndex) && !pIn->pTileInfo) {
         std::memset(&tileInfoNull, 0, sizeof(ADDR_TILEINFO));

         if (pOut->pTileInfo) {
            pIn->pTileInfo = pOut->pTileInfo;
         } else {
            pIn->pTileInfo = &tileInfoNull;
            pOut->pTileInfo = &tileInfoNull;
         }
      }

      returnCode = HwlSetupTileCfg(pIn->tileIndex, pIn->pTileInfo, &pIn->tileMode, &pIn->tileType);

      if (returnCode == ADDR_OK) {
         pOut->pixelBits = pIn->bpp;

         if (pIn->format != ADDR_FMT_INVALID) {
            auto bpp = mElemLib->GetBitsPerPixel(pIn->format, &elemMode, &expandX, &expandY, nullptr);

            if (elemMode == ADDR_EXPANDED && expandX == 3 && pIn->tileMode == ADDR_TM_LINEAR_ALIGNED) {
               pIn->flags.linearWA = 1;
            }

            mElemLib->AdjustSurfaceInfo(elemMode, expandX, expandY, &bpp, &width, &height);

            pIn->width = width;
            pIn->height = height;
            pIn->bpp = bpp;
         } else if (pIn->bpp != 0) {
            pIn->width = std::max<uint32_t>(1u, pIn->width);
            pIn->height = std::max<uint32_t>(1u, pIn->height);
         } else {
            returnCode = ADDR_INVALIDPARAMS;
         }
      }

      if (returnCode == ADDR_OK) {
         returnCode = HwlComputeSurfaceInfo(pIn, pOut);
      }

      if (returnCode == ADDR_OK) {
         pOut->bpp = pIn->bpp;
         pOut->pixelPitch = pOut->pitch;
         pOut->pixelHeight = pOut->height;

         if (pIn->format != ADDR_FMT_INVALID && (!pIn->flags.linearWA || pIn->mipLevel == 0)) {
            mElemLib->RestoreSurfaceInfo(elemMode, expandX, expandY, &bpp, &pOut->pixelPitch, &pOut->pixelHeight);
         }

         if (pIn->flags.qbStereo && pOut->pStereoInfo) {
            ComputeQbStereoInfo(pOut);
         }

         if (sliceFlags) {
            if (sliceFlags == 1) {
               pOut->sliceSize = BITS_TO_BYTES(static_cast<size_t>(pOut->height) * pOut->pitch * pOut->bpp * pIn->numSamples);
            }
         } else if (pIn->flags.volume) {
            pOut->sliceSize = static_cast<uint32_t>(pOut->surfSize);
         } else {
            pOut->sliceSize = static_cast<uint32_t>(pOut->surfSize / pOut->depth);

            if (pIn->numSlices > 1) {
               if (pIn->slice == (pIn->numSlices - 1)) {
                  pOut->sliceSize += pOut->sliceSize * (pOut->depth - pIn->numSlices);
               }
            }
         }

         pOut->pitchTileMax = (pOut->pitch / 8) - 1;
         pOut->heightTileMax = (pOut->height / 8) - 1;
         pOut->sliceTileMax = pOut->pitch * (pOut->height / 64) - 1;
      }
   }

   return returnCode;
}


/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceAddrFromCoordLinear
*
*   @brief
*       Compute address from coord for linear surface
*
*   @return
*       Address in bytes
***************************************************************************************************
*/
uint64_t
AddrLib::ComputeSurfaceAddrFromCoordLinear(uint32_t x,
                                           uint32_t y,
                                           uint32_t slice,
                                           uint32_t sample,
                                           uint32_t bpp,
                                           uint32_t pitch,
                                           uint32_t height,
                                           uint32_t numSlices,
                                           uint32_t *pBitPosition) const
{
   auto sliceSize = static_cast<uint64_t>(pitch) * height;

   auto sliceOffset = sliceSize * (slice + sample * numSlices);
   auto rowOffset = static_cast<uint64_t>(y) * pitch;
   auto pixOffset = static_cast<uint64_t>(x);

   auto addr = (sliceOffset + rowOffset + pixOffset) * bpp;

   *pBitPosition = static_cast<uint32_t>(addr % 8);
   addr /= 8;

   return addr;
}


/**
***************************************************************************************************
*   AddrLib::ComputePixelIndexWithinMicroTile
*
*   @brief
*       Compute the pixel index inside a micro tile of surface
*
*   @return
*       Pixel index
***************************************************************************************************
*/
uint32_t
AddrLib::ComputePixelIndexWithinMicroTile(uint32_t x,
                                          uint32_t y,
                                          uint32_t z,
                                          uint32_t bpp,
                                          AddrTileMode tileMode,
                                          AddrTileType tileType) const
{
   uint32_t pixelBit0 = 0;
   uint32_t pixelBit1 = 0;
   uint32_t pixelBit2 = 0;
   uint32_t pixelBit3 = 0;
   uint32_t pixelBit4 = 0;
   uint32_t pixelBit5 = 0;
   uint32_t pixelBit6 = 0;
   uint32_t pixelBit7 = 0;
   uint32_t pixelBit8 = 0;
   uint32_t pixelNumber;

   uint32_t x0 = _BIT(x, 0);
   uint32_t x1 = _BIT(x, 1);
   uint32_t x2 = _BIT(x, 2);
   uint32_t y0 = _BIT(y, 0);
   uint32_t y1 = _BIT(y, 1);
   uint32_t y2 = _BIT(y, 2);
   uint32_t z0 = _BIT(z, 0);
   uint32_t z1 = _BIT(z, 1);
   uint32_t z2 = _BIT(z, 2);

   auto thickness = ComputeSurfaceThickness(tileMode);

   if (tileType == ADDR_THICK_TILING) {
      pixelBit0 = x0;
      pixelBit1 = y0;
      pixelBit2 = z0;
      pixelBit3 = x1;
      pixelBit4 = y1;
      pixelBit5 = z1;
      pixelBit6 = x2;
      pixelBit7 = y2;
   } else {
      if (tileType == ADDR_NON_DISPLAYABLE) {
         pixelBit0 = x0;
         pixelBit1 = y0;
         pixelBit2 = x1;
         pixelBit3 = y1;
         pixelBit4 = x2;
         pixelBit5 = y2;
      } else {
         switch (bpp) {
         case 8:
            pixelBit0 = x0;
            pixelBit1 = x1;
            pixelBit2 = x2;
            pixelBit3 = y1;
            pixelBit4 = y0;
            pixelBit5 = y2;
            break;
         case 16:
            pixelBit0 = x0;
            pixelBit1 = x1;
            pixelBit2 = x2;
            pixelBit3 = y0;
            pixelBit4 = y1;
            pixelBit5 = y2;
            break;
         case 64:
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = x1;
            pixelBit3 = x2;
            pixelBit4 = y1;
            pixelBit5 = y2;
            break;
         case 128:
            pixelBit0 = y0;
            pixelBit1 = x0;
            pixelBit2 = x1;
            pixelBit3 = x2;
            pixelBit4 = y1;
            pixelBit5 = y2;
            break;
         case 32:
         case 96:
         default:
            pixelBit0 = x0;
            pixelBit1 = x1;
            pixelBit2 = y0;
            pixelBit3 = x2;
            pixelBit4 = y1;
            pixelBit5 = y2;
            break;
         }
      }

      if (thickness > 1) {
         pixelBit6 = z0;
         pixelBit7 = z1;
      }
   }

   if (thickness == 8) {
      pixelBit8 = z2;
   }

   pixelNumber = ((pixelBit0) |
                  (pixelBit1 << 1) |
                  (pixelBit2 << 2) |
                  (pixelBit3 << 3) |
                  (pixelBit4 << 4) |
                  (pixelBit5 << 5) |
                  (pixelBit6 << 6) |
                  (pixelBit7 << 7) |
                  (pixelBit8 << 8));

   return pixelNumber;
}


/**
***************************************************************************************************
*   AddrLib::ComputeSurfaceInfo
*
*   @brief
*       Interface function stub of AddrComputeSurfaceInfo.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::ComputeSurfaceAddrFromCoord(const ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn,
                                     ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;

   if (GetFillSizeFieldsFlags()) {
      if (pIn->size != sizeof(ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT) || pOut->size != sizeof(ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT)) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (returnCode == ADDR_OK) {
      ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT input;
      ADDR_TILEINFO tileInfoNull;

      if (UseTileIndex(pIn->tileIndex)) {
         std::memset(&tileInfoNull, 0, sizeof(ADDR_TILEINFO));
         input = *pIn;

         if (!pIn->pTileInfo) {
            input.pTileInfo = &tileInfoNull;
         }

         returnCode = HwlSetupTileCfg(input.tileIndex, input.pTileInfo, &input.tileMode, &input.tileType);
         pIn = &input;
      }

      if (returnCode == ADDR_OK) {
         returnCode = HwlComputeSurfaceAddrFromCoord(pIn, pOut);
      }
   }

   return returnCode;
}


/**
***************************************************************************************************
*   AddrLib::ExtractBankPipeSwizzle
*
*   @brief
*       Interface function stub of AddrExtractBankPipeSwizzle.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::ExtractBankPipeSwizzle(const ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn,
                                ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;

   if (GetFillSizeFieldsFlags()) {
      if (pIn->size != sizeof(ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT) || pOut->size != sizeof(ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT)) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (returnCode == ADDR_OK) {
      ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT input;
      ADDR_TILEINFO tileInfoNull;

      if (UseTileIndex(pIn->tileIndex)) {
         std::memset(&tileInfoNull, 0, sizeof(ADDR_TILEINFO));
         input = *pIn;

         if (!pIn->pTileInfo) {
            input.pTileInfo = &tileInfoNull;
         }

         returnCode = HwlSetupTileCfg(input.tileIndex, input.pTileInfo, nullptr, nullptr);
         pIn = &input;
      }

      if (returnCode == ADDR_OK) {
         returnCode = HwlExtractBankPipeSwizzle(pIn, pOut);
      }
   }

   return returnCode;
}


/**
***************************************************************************************************
*   AddrLib::HwlComputeTileDataWidthAndHeightLinear
*
*   @brief
*       Compute the squared cache shape for per-tile data (CMASK and HTILE) for linear layout
*
*   @return
*       N/A
*
*   @note
*       MacroWidth and macroHeight are measured in pixels
***************************************************************************************************
*/
void
AddrLib::HwlComputeTileDataWidthAndHeightLinear(uint32_t *pMacroWidth,
                                                uint32_t *pMacroHeight,
                                                uint32_t bpp,
                                                ADDR_TILEINFO *pTileInfo) const
{
   *pMacroWidth = (8 * 512) / bpp;
   *pMacroHeight = 8 * mPipes;
}


/**
***************************************************************************************************
*   AddrLib::ComputeTileDataWidthAndHeight
*
*   @brief
*       Compute the squared cache shape for per-tile data (CMASK and HTILE)
*
*   @return
*       N/A
*
*   @note
*       MacroWidth and macroHeight are measured in pixels
***************************************************************************************************
*/
void
AddrLib::ComputeTileDataWidthAndHeight(uint32_t bpp,
                                       uint32_t cacheBits,
                                       ADDR_TILEINFO *pTileInfo,
                                       uint32_t *pMacroWidth,
                                       uint32_t *pMacroHeight) const
{
   auto height = 1;
   auto width = cacheBits / bpp;
   auto pipes = GetNumPipes(pTileInfo);

   while ((width > height * 2 * pipes) && !(width & 1)) {
      width /= 2;
      height *= 2;
   }

   *pMacroWidth = 8 * width;
   *pMacroHeight = 8 * height * pipes;
}


/**
***************************************************************************************************
*   AddrLib::ComputeHtileInfo
*
*   @brief
*       Compute htile pitch,width, bytes per 2D slice
*
*   @return
*       Htile bpp i.e. How many bits for an 8x8 tile
*       Also returns by output parameters:
*       *Htile pitch, height, total size in bytes, macro-tile dimensions and slice size*
***************************************************************************************************
*/
uint32_t
AddrLib::ComputeHtileInfo(uint32_t pitchIn,
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
                          uint64_t *pSliceBytes,
                          uint32_t *pBaseAlign) const
{
   auto pipes = GetNumPipes(pTileInfo);
   auto bpp = HwlComputeHtileBpp(isWidth8, isHeight8);
   auto macroWidth = uint32_t { 0 };
   auto macroHeight = uint32_t { 0 };

   if (isLinear) {
      HwlComputeTileDataWidthAndHeightLinear(&macroWidth, &macroHeight, bpp, pTileInfo);
   } else {
      ComputeTileDataWidthAndHeight(bpp, HtileCacheBits, pTileInfo, &macroWidth, &macroHeight);
   }

   *pPitchOut = PowTwoAlign(pitchIn, macroWidth);
   *pHeightOut = PowTwoAlign(heightIn, macroHeight);

   auto baseAlign = HwlComputeHtileBaseAlign(isLinear, pipes);
   auto surfBytes = HwlComputeHtileBytes(*pPitchOut,
                                         *pHeightOut,
                                         bpp,
                                         isLinear,
                                         numSlices,
                                         baseAlign);


   auto sliceBytes = BITS_TO_BYTES(static_cast<uint64_t>(*pHeightOut) * (*pPitchOut) * bpp / 64);

   if (pHtileBytes) {
      *pHtileBytes = surfBytes;
   }

   if (pMacroWidth) {
      *pMacroWidth = macroWidth;
   }

   if (pMacroHeight) {
      *pMacroHeight = macroHeight;
   }

   if (pSliceBytes) {
      *pSliceBytes = sliceBytes;
   }

   if (pBaseAlign) {
      *pBaseAlign = baseAlign;
   }

   return bpp;
}


/**
***************************************************************************************************
*   AddrLib::ComputeHtileInfo
*
*   @brief
*       Interface function stub of AddrComputeHtilenfo
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::ComputeHtileInfo(const ADDR_COMPUTE_HTILE_INFO_INPUT *pIn,
                          ADDR_COMPUTE_HTILE_INFO_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;
   auto isWidth8 = (pIn->blockWidth == 8);
   auto isHeight8 = (pIn->blockHeight == 8);

   if (GetFillSizeFieldsFlags()) {
      if (pIn->size != sizeof(ADDR_COMPUTE_HTILE_INFO_INPUT) || pOut->size != sizeof(ADDR_COMPUTE_HTILE_INFO_OUTPUT)) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (returnCode == ADDR_OK) {
      ADDR_COMPUTE_HTILE_INFO_INPUT input;
      ADDR_TILEINFO tileInfoNull;

      if (UseTileIndex(pIn->tileIndex)) {
         std::memset(&tileInfoNull, 0, sizeof(ADDR_TILEINFO));
         input = *pIn;

         if (!pIn->pTileInfo) {
            input.pTileInfo = &tileInfoNull;
         }

         returnCode = HwlSetupTileCfg(input.tileIndex, input.pTileInfo, nullptr, nullptr);
         pIn = &input;
      }

      if (returnCode == ADDR_OK) {
         pOut->bpp = ComputeHtileInfo(pIn->pitch,
                                      pIn->height,
                                      pIn->numSlices,
                                      pIn->isLinear,
                                      isWidth8,
                                      isHeight8,
                                      pIn->pTileInfo,
                                      &pOut->pitch,
                                      &pOut->height,
                                      &pOut->htileBytes,
                                      &pOut->macroWidth,
                                      &pOut->macroHeight,
                                      nullptr,
                                      &pOut->baseAlign);
      }
   }

   return returnCode;
}


/**
***************************************************************************************************
*   AddrLib::ComputeSliceTileSwizzle
*
*   @brief
*       Interface function stub of ComputeSliceTileSwizzle.
*
*   @return
*       ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrLib::ComputeSliceTileSwizzle(const ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn,
                                 ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut) const
{
   ADDR_E_RETURNCODE returnCode = ADDR_OK;

   if (GetFillSizeFieldsFlags()) {
      if (pIn->size != sizeof(ADDR_COMPUTE_SLICESWIZZLE_INPUT) || pOut->size != sizeof(ADDR_COMPUTE_SLICESWIZZLE_OUTPUT)) {
         returnCode = ADDR_PARAMSIZEMISMATCH;
      }
   }

   if (returnCode == ADDR_OK) {
      ADDR_COMPUTE_SLICESWIZZLE_INPUT input;
      ADDR_TILEINFO tileInfoNull;

      if (UseTileIndex(pIn->tileIndex)) {
         std::memset(&tileInfoNull, 0, sizeof(ADDR_TILEINFO));
         input = *pIn;

         if (!pIn->pTileInfo) {
            input.pTileInfo = &tileInfoNull;
         }

         returnCode = HwlSetupTileCfg(input.tileIndex, input.pTileInfo, nullptr, nullptr);
         pIn = &input;
      }

      if (returnCode == ADDR_OK) {
         returnCode = HwlComputeSliceTileSwizzle(pIn, pOut);
      }
   }

   return returnCode;
}
