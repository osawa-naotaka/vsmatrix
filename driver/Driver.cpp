/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    Driver.cpp

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include <wdm.h>
#include <windef.h>
#include "public.h"
#include "cpp_utils.h"
#include <wdf.h>

_Use_decl_annotations_
void AudioCodecDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    PAGED_CODE();

    if (!Driver)
    {
        ASSERT(FALSE);
        return;
    }

    return;
}

INIT_CODE_SEG
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG           wdfCfg;
    ACX_DRIVER_CONFIG           acxCfg;
    WDFDRIVER                   driver;
    NTSTATUS                    status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES       attributes;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    WDF_DRIVER_CONFIG_INIT(&wdfCfg, Codec_EvtBusDeviceAdd);
    wdfCfg.EvtDriverUnload = AudioCodecDriverUnload;

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &wdfCfg, &driver);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfDriverCreate failed: 0x%x\n", status));
        return status;
    }

    //
    // Initializing the ACX driver configuration struct which contains size and flags
    // elements. 
    //
    ACX_DRIVER_CONFIG_INIT(&acxCfg);

    // 
    // The driver calls this DDI in its DriverEntry callback after creating the WDF driver
    // object. ACX uses this call to apply any post driver settings.
    //
    status = AcxDriverInitialize(driver, &acxCfg);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: AcxDriverInitialize failed: 0x%x\n", status));
        return status;
    }

    return status;
}
