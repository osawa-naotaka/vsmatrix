/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    Capture.cpp

Abstract:


Environment:

    Kernel-mode Driver Framework

--*/


#include <wdm.h>
#include <windef.h>
#include "Public.h"
#include <ks.h>
#include <mmsystem.h>
#include <ksmedia.h>
#include "AudioFormats.h"
#include "cpp_utils.h"

PAGED_CODE_SEG
NTSTATUS
CodecC_AddStaticCapture(
    _In_ WDFDEVICE              Device,
    _In_ const GUID* ComponentGuid,
    _In_ const GUID* MicCustomName,
    _In_ const UNICODE_STRING* CircuitName
)
/*++

Routine Description:

    Creates the static capture circuit (pictured below)
    and adds it to the device context. This is called
    when a new device is detected and the AddDevice
    call is made by the pnp manager.

    ******************************************************
    * Capture Circuit                                    *
    *                                                    *
    *              +-----------------------+             *
    *              |                       |             *
    *              |    +-------------+    |             *
    * Host  ------>|    | Volume Node |    |---> Bridge  *
    * Pin          |    +-------------+    |      Pin    *
    *              |                       |             *
    *              +-----------------------+             *
    *                                                    *
    ******************************************************

Return Value:

    NTSTATUS

--*/
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ComponentGuid);
    UNREFERENCED_PARAMETER(MicCustomName);
    UNREFERENCED_PARAMETER(CircuitName);

    NTSTATUS                        status = STATUS_SUCCESS;
    return status;
}

PAGED_CODE_SEG
NTSTATUS
CodecC_CircuitCleanup(
    _In_ ACXCIRCUIT Circuit
)
{
    UNREFERENCED_PARAMETER(Circuit);

    return STATUS_SUCCESS;
}
