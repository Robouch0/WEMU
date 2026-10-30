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
* @file  addrelemlib.h
* @brief Contains the class for element/pixel related functions
***************************************************************************************************
*/

#pragma once
#include "addrlib/addrinterface.h"
#include "addrobject.h"
#include "addrcommon.h"

class AddrLib;

enum AddrDepthPlanarType : uint32_t
{
   ADDR_DEPTH_PLANAR_NONE = 0,
   ADDR_DEPTH_PLANAR_R600 = 1,
   ADDR_DEPTH_PLANAR_R800 = 2,
};

enum AddrElemMode : uint32_t
{
   ADDR_ROUND_BY_HALF = 0x0,
   ADDR_ROUND_TRUNCATE = 0x1,
   ADDR_ROUND_DITHER = 0x2,
   ADDR_UNCOMPRESSED = 0x3,
   ADDR_EXPANDED = 0x4,
   ADDR_PACKED_STD = 0x5,
   ADDR_PACKED_REV = 0x6,
   ADDR_PACKED_GBGR = 0x7,
   ADDR_PACKED_BGRG = 0x8,
   ADDR_PACKED_BC1 = 0x9,
   ADDR_PACKED_BC2 = 0xA,
   ADDR_PACKED_BC3 = 0xB,
   ADDR_PACKED_BC4 = 0xC,
   ADDR_PACKED_BC5 = 0xD,
   ADDR_ZPLANE_R5XX = 0xE,
   ADDR_ZPLANE_R6XX = 0xF,
   ADDR_END_ELEMENT = 0x10,
};


/**
***************************************************************************************************
* @brief This class contains asic indepentent element related attributes and operations
***************************************************************************************************
*/
class AddrElemLib : AddrObject
{
public:
   AddrElemLib(AddrLib *pAddrLib);

   static AddrElemLib *
   Create(AddrLib *pAddrLib, const ADDR_CREATE_INPUT *pInput);

   void
   SetConfigFlags(ADDR_CONFIG_FLAGS flags);

   uint32_t
   GetBitsPerPixel(AddrFormat format,
                     AddrElemMode *pElemMode,
                     uint32_t *pExpandX,
                     uint32_t *pExpandY,
                     uint32_t *pUnusedBits);

   void
   AdjustSurfaceInfo(AddrElemMode elemMode,
                     uint32_t expandX,
                     uint32_t expandY,
                     uint32_t *pBpp,
                     uint32_t *pWidth,
                     uint32_t *pHeight);

   void
   RestoreSurfaceInfo(AddrElemMode elemMode,
                      uint32_t expandX,
                      uint32_t expandY,
                      uint32_t *pBpp,
                      uint32_t *pWidth,
                      uint32_t *pHeight);

   bool
   IsBlockCompressed(AddrFormat format);

protected:
   uint32_t mFp16ExportNorm;
   AddrDepthPlanarType mDepthPlanarType;
   ADDR_CONFIG_FLAGS mConfigFlags;
   AddrLib *mAddrLib;
};
