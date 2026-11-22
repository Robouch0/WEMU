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
* @file  addrinterface.cpp
* @brief Contains the addrlib interface functions
***************************************************************************************************
*/

#include "addrlib/addrinterface.h"
#include "core/addrlib.h"


/**
***************************************************************************************************
*   AddrCreate
*
*   @brief
*       Create address lib object
*
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrCreate(const ADDR_CREATE_INPUT *pCreateIn, ADDR_CREATE_OUTPUT *pCreateOut)
{
   return AddrLib::Create(pCreateIn, pCreateOut);
}


/**
***************************************************************************************************
*   AddrDestroy
*
*   @brief
*       Destroy address lib object
*
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrDestroy(ADDR_HANDLE hLib)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   pLib->Destroy();
   return ADDR_OK;
}


/**
***************************************************************************************************
*   AddrComputeSurfaceInfo
*
*   @brief
*       Calculate surface width/height/depth/alignments and suitable tiling mode
*
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeSurfaceInfo(ADDR_HANDLE hLib, ADDR_COMPUTE_SURFACE_INFO_INPUT *pIn, ADDR_COMPUTE_SURFACE_INFO_OUTPUT *pOut)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   return pLib->ComputeSurfaceInfo(pIn, pOut);
}


/**
***************************************************************************************************
*   AddrComputeSurfaceAddrFromCoord
*
*   @brief
*       Compute surface address according to coordinates
*
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeSurfaceAddrFromCoord(ADDR_HANDLE hLib, ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT *pIn, ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT *pOut)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   return pLib->ComputeSurfaceAddrFromCoord(pIn, pOut);
}


/**
***************************************************************************************************
*   AddrExtractBankPipeSwizzle
*
*   @brief
*       Extract Bank and Pipe swizzle from base256b
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrExtractBankPipeSwizzle(ADDR_HANDLE hLib, ADDR_EXTRACT_BANKPIPE_SWIZZLE_INPUT *pIn, ADDR_EXTRACT_BANKPIPE_SWIZZLE_OUTPUT *pOut)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   return pLib->ExtractBankPipeSwizzle(pIn, pOut);
}


/**
***************************************************************************************************
*   AddrComputeHtileInfo
*
*   @brief
*       Compute Htile pitch, height, base alignment and size in bytes
*
*   @return
*       ADDR_OK if successful, otherwise an error code of ADDR_E_RETURNCODE
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrComputeHtileInfo(ADDR_HANDLE hLib, ADDR_COMPUTE_HTILE_INFO_INPUT *pIn, ADDR_COMPUTE_HTILE_INFO_OUTPUT *pOut)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   return pLib->ComputeHtileInfo(pIn, pOut);
}


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
AddrComputeSliceSwizzle(ADDR_HANDLE hLib, ADDR_COMPUTE_SLICESWIZZLE_INPUT *pIn, ADDR_COMPUTE_SLICESWIZZLE_OUTPUT *pOut)
{
   auto pLib = AddrLib::GetAddrLib(hLib);

   if (!pLib) {
      return ADDR_ERROR;
   }

   return pLib->ComputeSliceTileSwizzle(pIn, pOut);
}
