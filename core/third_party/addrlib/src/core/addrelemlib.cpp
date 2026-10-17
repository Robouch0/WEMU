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
* @file  addrelemlib.cpp
* @brief Contains the class implementation for element/pixel related functions
***************************************************************************************************
*/

#include <algorithm>
#include <new>
#include "addrelemlib.h"
#include "addrlib.h"


/**
***************************************************************************************************
*   AddrElemLib::AddrElemLib
*
*   @brief
*       constructor
*
*   @return
*       N/A
***************************************************************************************************
*/
AddrElemLib::AddrElemLib(AddrLib *pAddrLib) :
   mAddrLib(pAddrLib)
{
   auto family = pAddrLib->GetAddrChipFamily();

   switch (family) {
   case ADDR_CHIP_FAMILY_R6XX:
      mDepthPlanarType = ADDR_DEPTH_PLANAR_R600;
      mFp16ExportNorm = 0;
      break;
   case ADDR_CHIP_FAMILY_R7XX:
      mDepthPlanarType = ADDR_DEPTH_PLANAR_R600;
      mFp16ExportNorm = 1;
      break;
   case ADDR_CHIP_FAMILY_R8XX:
   case ADDR_CHIP_FAMILY_R9XX:
   default:
      mDepthPlanarType = ADDR_DEPTH_PLANAR_R800;
      mFp16ExportNorm = 1;
      break;
   }

   mConfigFlags.value = 0;
}


/**
***************************************************************************************************
*   AddrElemLib::Create
*
*   @brief
*       Creates and initializes AddrLib object.
*
*   @return
*       Returns point to ADDR_CREATEINFO if successful.
***************************************************************************************************
*/
AddrElemLib *
AddrElemLib::Create(AddrLib *pAddrLib, const ADDR_CREATE_INPUT *pInput)
{
   AddrElemLib *pElemLib = nullptr;

   if (pAddrLib) {
      auto memory = AddrObject::ClientAlloc(sizeof(AddrElemLib), pInput->hClient);
      pElemLib = new (memory) AddrElemLib(pAddrLib);
   }

   if (pElemLib) {
      pElemLib->mClient = pInput->hClient;
      pElemLib->mDebugPrint = pInput->callbacks.debugPrint;
   }

   return pElemLib;
}


/**
***************************************************************************************************
*   AddrElemLib::SetConfigFlags
*
*   @brief
*       Set the ADDR_CONFIG_FLAGS
*
*   @return
*       N/A
***************************************************************************************************
*/
void
AddrElemLib::SetConfigFlags(ADDR_CONFIG_FLAGS flags)
{
   mConfigFlags = flags;
}


/**
***************************************************************************************************
*   AddrElemLib::GetBitsPerPixel
*
*   @brief
*       Compute the total bits per element according to a format
*       code. For compressed formats, this is not the same as
*       the number of bits per decompressed element.
*
*   @return
*       Bits per pixel
***************************************************************************************************
*/
uint32_t
AddrElemLib::GetBitsPerPixel(AddrFormat format,
                             AddrElemMode *pElemMode,
                             uint32_t *pExpandX,
                             uint32_t *pExpandY,
                             uint32_t *pUnusedBits)
{
   AddrElemMode elemMode = ADDR_UNCOMPRESSED;
   uint32_t bpp;
   uint32_t expandX = 1;
   uint32_t expandY = 1;
   uint32_t bitUnused = 0;

   switch (format) {
   case ADDR_FMT_INVALID:
      bpp = 0;
      break;
   case ADDR_FMT_8:
      bpp = 8;
      break;
   case ADDR_FMT_1_5_5_5:
   case ADDR_FMT_5_6_5:
   case ADDR_FMT_6_5_5:
   case ADDR_FMT_8_8:
   case ADDR_FMT_4_4_4_4:
   case ADDR_FMT_16:
   case ADDR_FMT_16_FLOAT:
      bpp = 16;
      break;
   case ADDR_FMT_GB_GR:
      elemMode = ADDR_PACKED_GBGR;
      bpp = 16;
      break;
   case ADDR_FMT_BG_RG:
      elemMode = ADDR_PACKED_BGRG;
      bpp = 16;
      break;
   case ADDR_FMT_8_8_8_8:
   case ADDR_FMT_2_10_10_10:
   case ADDR_FMT_10_11_11:
   case ADDR_FMT_11_11_10:
   case ADDR_FMT_16_16:
   case ADDR_FMT_16_16_FLOAT:
   case ADDR_FMT_32:
   case ADDR_FMT_32_FLOAT:
   case ADDR_FMT_24_8:
   case ADDR_FMT_24_8_FLOAT:
      bpp = 32;
      break;
   case ADDR_FMT_16_16_16_16:
   case ADDR_FMT_16_16_16_16_FLOAT:
   case ADDR_FMT_32_32:
   case ADDR_FMT_32_32_FLOAT:
   case ADDR_FMT_CTX1:
      bpp = 64;
      break;
   case ADDR_FMT_32_32_32_32:
   case ADDR_FMT_32_32_32_32_FLOAT:
      bpp = 128;
      break;
   case ADDR_FMT_1_REVERSED:
      elemMode = ADDR_PACKED_REV;
      expandX = 8;
      bpp = 1;
      break;
   case ADDR_FMT_1:
      elemMode = ADDR_PACKED_STD;
      expandX = 8;
      bpp = 1;
      break;
   case ADDR_FMT_4_4:
   case ADDR_FMT_3_3_2:
      bpp = 8;
      break;
   case ADDR_FMT_5_5_5_1:
      bpp = 16;
      break;
   case ADDR_FMT_8_24:
   case ADDR_FMT_8_24_FLOAT:
   case ADDR_FMT_10_11_11_FLOAT:
   case ADDR_FMT_11_11_10_FLOAT:
   case ADDR_FMT_10_10_10_2:
   case ADDR_FMT_32_AS_8:
   case ADDR_FMT_32_AS_8_8:
   case ADDR_FMT_5_9_9_9_SHAREDEXP:
      bpp = 32;
      break;
   case ADDR_FMT_X24_8_32_FLOAT:
      bpp = 64;
      bitUnused = 24;
      break;
   case ADDR_FMT_8_8_8:
      elemMode = ADDR_EXPANDED;
      expandX = 3;
      bpp = 24;
      break;
   case ADDR_FMT_16_16_16:
   case ADDR_FMT_16_16_16_FLOAT:
      elemMode = ADDR_EXPANDED;
      expandX = 3;
      bpp = 48;
      break;
   case ADDR_FMT_32_32_32_FLOAT:
   case ADDR_FMT_32_32_32:
      elemMode = ADDR_EXPANDED;
      expandX = 3;
      bpp = 96;
      break;
   case ADDR_FMT_BC1:
      elemMode = ADDR_PACKED_BC1;
      expandX = 4;
      expandY = 4;
      bpp = 64;
      break;
   case ADDR_FMT_BC4:
      elemMode = ADDR_PACKED_BC4;
      expandX = 4;
      expandY = 4;
      bpp = 64;
      break;
   case ADDR_FMT_BC2:
      elemMode = ADDR_PACKED_BC2;
      expandX = 4;
      expandY = 4;
      bpp = 128;
      break;
   case ADDR_FMT_BC3:
      elemMode = ADDR_PACKED_BC3;
      expandX = 4;
      expandY = 4;
      bpp = 128;
      break;
   case ADDR_FMT_BC5:
   case ADDR_FMT_BC6:
   case ADDR_FMT_BC7:
      elemMode = ADDR_PACKED_BC5;
      expandX = 4;
      expandY = 4;
      bpp = 128;
      break;
   default:
      bpp = 0;
      break;
   }

   if (pExpandX) {
      *pExpandX = expandX;
   }

   if (pExpandY) {
      *pExpandY = expandY;
   }

   if (pUnusedBits) {
      *pUnusedBits = bitUnused;
   }

   if (pElemMode) {
      *pElemMode = elemMode;
   }

   return bpp;
}


/**
***************************************************************************************************
*   AddrElemLib::AdjustSurfaceInfo
*
*   @brief
*       Adjust bpp/base pitch/width/height according to elemMode and expandX/Y
*
*   @return
*       N/A
***************************************************************************************************
*/
void
AddrElemLib::AdjustSurfaceInfo(AddrElemMode elemMode,
                               uint32_t expandX,
                               uint32_t expandY,
                               uint32_t *pBpp,
                               uint32_t *pWidth,
                               uint32_t *pHeight)
{
   bool bBCnFormat = false;

   if (pBpp) {
      auto bpp = *pBpp;

      switch (elemMode) {
      case ADDR_EXPANDED:
         *pBpp = bpp / expandX / expandY;
         break;
      case ADDR_PACKED_STD:
      case ADDR_PACKED_REV:
         *pBpp = expandY * expandX * bpp;
         break;
      case ADDR_PACKED_BC1:
      case ADDR_PACKED_BC4:
         *pBpp = 64;
         bBCnFormat = true;
         break;
      case ADDR_PACKED_BC2:
      case ADDR_PACKED_BC3:
      case ADDR_PACKED_BC5:
         bBCnFormat = true;
         *pBpp = 128;
         break;
      }
   }

   if (pWidth && pHeight) {
      auto width = *pWidth;
      auto height = *pHeight;

      if (expandX > 1 || expandY > 1) {
         if (elemMode == ADDR_EXPANDED) {
            width = expandX * width;
            height = expandY * height;
         } else if (bBCnFormat) {
            width = width / expandX;
            height = height / expandY;
         } else {
            width = (width + expandX - 1) / expandX;
            height = (height + expandY - 1) / expandY;
         }

         *pWidth = std::max<uint32_t>(1u, width);
         *pHeight = std::max<uint32_t>(1u, height);
      }
   }
}


/**
***************************************************************************************************
*   AddrElemLib::RestoreSurfaceInfo
*
*   @brief
*       Reverse operation of AdjustSurfaceInfo
*
*   @return
*       N/A
***************************************************************************************************
*/
void
AddrElemLib::RestoreSurfaceInfo(AddrElemMode elemMode,
                                uint32_t expandX,
                                uint32_t expandY,
                                uint32_t *pBpp,
                                uint32_t *pWidth,
                                uint32_t *pHeight)
{
   if (pBpp) {
      auto bpp = *pBpp;

      switch (elemMode) {
      case ADDR_EXPANDED:
         *pBpp = expandX * expandY * bpp;
         break;
      case ADDR_PACKED_STD:
      case ADDR_PACKED_REV:
         *pBpp = bpp / expandX / expandY;
         break;
      case ADDR_PACKED_BC1:
      case ADDR_PACKED_BC4:
         *pBpp = 64;
         break;
      case ADDR_PACKED_BC2:
      case ADDR_PACKED_BC3:
      case ADDR_PACKED_BC5:
         *pBpp = 128;
         break;
      default:
         break;
      }
   }

   if (pWidth && pHeight) {
      auto width = *pWidth;
      auto height = *pHeight;

      if (expandX > 1 || expandY > 1) {
         if (elemMode == ADDR_EXPANDED) {
            width /= expandX;
            height /= expandY;
         } else {
            width *= expandX;
            height *= expandY;
         }
      }

      *pWidth = std::max<uint32_t>(1u, width);
      *pHeight = std::max<uint32_t>(1u, height);
   }
}


/**
***************************************************************************************************
*   AddrElemLib::IsBlockCompressed
*
*   @brief
*       TRUE if this is block compressed format
*
*   @return
*       bool
***************************************************************************************************
*/
bool
AddrElemLib::IsBlockCompressed(AddrFormat format)
{
   return format >= ADDR_FMT_BC1 && format <= ADDR_FMT_BC7;
}
