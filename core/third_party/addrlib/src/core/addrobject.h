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
* @file  addrobject.h
* @brief Contains the AddrObject base class definition.
***************************************************************************************************
*/

#pragma once
#include "addrlib/addrtypes.h"
#include "addrcommon.h"


/**
***************************************************************************************************
* @brief This class is the base class for all ADDR class objects.
***************************************************************************************************
*/
class AddrObject
{
public:
   AddrObject();
   AddrObject(ADDR_CLIENT_HANDLE hClient);
   virtual ~AddrObject() = default;

   static void
   SetupSysMemFuncs(ADDR_ALLOCSYSMEM allocSysMem, ADDR_FREESYSMEM freeSysMem);

   void *
   AddrMalloc(uint32_t size);

   ADDR_E_RETURNCODE
   AddrFree(void *pVirtAddr);

   static void *
   ClientAlloc(uint32_t size, ADDR_CLIENT_HANDLE hClient);

   static ADDR_E_RETURNCODE
   ClientFree(void *pVirtAddr, ADDR_CLIENT_HANDLE hClient);

protected:
   ADDR_CLIENT_HANDLE mClient;
   ADDR_DEBUGPRINT mDebugPrint;

   static ADDR_ALLOCSYSMEM mAllocSysMem;
   static ADDR_FREESYSMEM mFreeSysMem;
};
