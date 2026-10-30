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
* @file  addrobject.cpp
* @brief Contains the AddrObject base class implementation.
***************************************************************************************************
*/

#include <cstring>
#include "addrobject.h"

ADDR_ALLOCSYSMEM
AddrObject::mAllocSysMem = nullptr;

ADDR_FREESYSMEM
AddrObject::mFreeSysMem = nullptr;


/**
***************************************************************************************************
*   AddrObject::AddrObject
*
*   @brief
*       Constructor for the AddrObject class.
***************************************************************************************************
*/
AddrObject::AddrObject() :
   mClient(nullptr),
   mDebugPrint(nullptr)
{
}


/**
***************************************************************************************************
*   AddrObject::AddrObject
*
*   @brief
*       Constructor for the AddrObject class.
***************************************************************************************************
*/
AddrObject::AddrObject(ADDR_CLIENT_HANDLE hClient) :
   mClient(hClient),
   mDebugPrint(nullptr)
{
}

void
AddrObject::SetupSysMemFuncs(ADDR_ALLOCSYSMEM allocSysMem, ADDR_FREESYSMEM freeSysMem)
{
   mAllocSysMem = allocSysMem;
   mFreeSysMem = freeSysMem;
}


/**
***************************************************************************************************
*   AddrObject::AddrMalloc
*
*   @brief
*       A wrapper of ClientAlloc
***************************************************************************************************
*/
void *
AddrObject::AddrMalloc(uint32_t size)
{
   return ClientAlloc(size, mClient);
}


/**
***************************************************************************************************
*   AddrObject::AddrFree
*
*   @brief
*       A wrapper of ClientFree
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrObject::AddrFree(void *pVirtAddr)
{
   return ClientFree(pVirtAddr, mClient);
}


/**
***************************************************************************************************
*   AddrObject::ClientAlloc
*
*   @brief
*       Calls instanced allocSysMem inside AddrClient
***************************************************************************************************
*/
void *
AddrObject::ClientAlloc(uint32_t size, ADDR_CLIENT_HANDLE hClient)
{
   ADDR_ALLOCSYSMEM_INPUT input;
   memset(&input, 0, sizeof(input));

   if (!mAllocSysMem) {
      return nullptr;
   }

   input.size = sizeof(ADDR_ALLOCSYSMEM_INPUT);
   input.hClient = hClient;
   input.sizeInBytes = size;
   return mAllocSysMem(&input);
}


/**
***************************************************************************************************
*   AddrObject::ClientFree
*
*   @brief
*       Calls freeSysMem inside AddrClient
***************************************************************************************************
*/
ADDR_E_RETURNCODE
AddrObject::ClientFree(void *pVirtAddr, ADDR_CLIENT_HANDLE hClient)
{
   ADDR_FREESYSMEM_INPUT input;
   memset(&input, 0, sizeof(input));

   if (!mAllocSysMem) {
      return ADDR_ERROR;
   }

   input.size = sizeof(ADDR_FREESYSMEM_INPUT);
   input.hClient = hClient;
   input.pVirtAddr = pVirtAddr;
   return mFreeSysMem(&input);
}
