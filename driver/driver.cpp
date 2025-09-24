

#include <ntddk.h>
#include <wdf.h>
#include <acx.h>

#define RETURN_NTSTATUS_IF_FAILED(X) \
    status = X; \
    if (!NT_SUCCESS(status)) { \
        KdPrint(("vsmatrix: function call failed: 0x%x at File:%s, Line:%d\n", status, __FILE__, __LINE__)); \
        return status; \
    }

#define RETURN_NTSTATUS_IF_FAILED_WITH_CLEANUP(X) \
    status = X; \
    if (!NT_SUCCESS(status)) { \
        KdPrint(("vsmatrix: function call failed: 0x%x at File:%s, Line:%d\n", status, __FILE__, __LINE__)); \
        goto cleanup; \
    }

// デバイスコンテキスト
typedef struct _DEVICE_CONTEXT {
    ULONG SampleRate;
    ULONG CurrentSample;
    BOOLEAN HighState;
} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

// ストリームコンテキスト
typedef struct _STREAM_CONTEXT {
    ACXDATAFORMAT DataFormat;
    BOOLEAN Running;
    PACX_RTPACKET Packet;
} STREAM_CONTEXT, * PSTREAM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STREAM_CONTEXT, GetStreamContext)

// 前方宣言
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_ACX_CIRCUIT_CREATE_STREAM EvtCircuitCreateStream;
EVT_ACX_STREAM_PREPARE_HARDWARE EvtStreamPrepareHardware;
EVT_ACX_STREAM_RUN EvtStreamRun;
EVT_ACX_STREAM_PAUSE EvtStreamPause;
EVT_WDF_TIMER EvtTimerFunc;
EVT_ACX_STREAM_GET_HW_LATENCY EvtStreamGetHwLatency;
EVT_ACX_STREAM_GET_CAPTURE_PACKET EvtStreamGetCapturePacket;
EVT_ACX_STREAM_GET_CURRENT_PACKET EvtStreamGetCurrentPacket;


// {75BD0EC5-B011-44F9-B26B-43FEB527313B}
static const GUID COMPONENT_GUID =
{ 0x75bd0ec5, 0xb011, 0x44f9, { 0xb2, 0x6b, 0x43, 0xfe, 0xb5, 0x27, 0x31, 0x3b } };


DECLARE_CONST_UNICODE_STRING(CIRCUIT_NAME, L"vsmRectWave");

// ドライバエントリポイント
extern "C" NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    KdPrint(("vsmatrix: DriverEntry called.\n"));

    WDFDRIVER driver;
    WDF_DRIVER_CONFIG wdfConfig;
    WDF_DRIVER_CONFIG_INIT(&wdfConfig, EvtDriverDeviceAdd);

    RETURN_NTSTATUS_IF_FAILED(WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &wdfConfig, &driver));

    ACX_DRIVER_CONFIG acxCfg;
    ACX_DRIVER_CONFIG_INIT(&acxCfg);

    RETURN_NTSTATUS_IF_FAILED(AcxDriverInitialize(driver, &acxCfg));

    KdPrint(("vsmatrix: DriverEntry finished.\n"));

    return status;
}

// デバイス追加コールバック
NTSTATUS EvtDriverDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("vsmatrix: EvtDriverDeviceAdd called.\n"));

    NTSTATUS status = STATUS_SUCCESS;

    // ACXデバイス初期化, PnP設定の前に初期化する
    ACX_DEVICEINIT_CONFIG devInitCfg;
    ACX_DEVICEINIT_CONFIG_INIT(&devInitCfg);

    RETURN_NTSTATUS_IF_FAILED(AcxDeviceInitInitialize(DeviceInit, &devInitCfg));

    // PnP設定
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    // デバイス作成
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

    WDFDEVICE device;
    RETURN_NTSTATUS_IF_FAILED(WdfDeviceCreate(&DeviceInit, &attributes, &device));

    // デバイスコンテキスト初期化
    auto deviceContext = GetDeviceContext(device);
    ASSERT(deviceContext != nullptr);

    deviceContext->SampleRate = 48000;  // 48kHz
    deviceContext->CurrentSample = 0;
    deviceContext->HighState = FALSE;


    // ACXデバイス初期化
    ACX_DEVICE_CONFIG devCfg;
    ACX_DEVICE_CONFIG_INIT(&devCfg);

    RETURN_NTSTATUS_IF_FAILED(AcxDeviceInitialize(device, &devCfg));

    //
    // サーキット作成
    //

    PACXCIRCUIT_INIT circuitInit = nullptr;
    circuitInit = AcxCircuitInitAllocate(device);
    if (circuitInit == nullptr) {
        KdPrint(("vsmatrix: function call failed: 0x%x at File:%s, Line:%d\n", status, __FILE__, __LINE__)); \
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // set Component GUID
    AcxCircuitInitSetComponentId(circuitInit, &COMPONENT_GUID);

    // set Circuit Name
    RETURN_NTSTATUS_IF_FAILED_WITH_CLEANUP(AcxCircuitInitAssignName(circuitInit, &CIRCUIT_NAME));

    // ストリーム作成コールバック設定
    RETURN_NTSTATUS_IF_FAILED_WITH_CLEANUP(AcxCircuitInitAssignAcxCreateStreamCallback(circuitInit, EvtCircuitCreateStream));

    //
    // The driver uses this DDI to create a new ACX circuit.
    //
    WDF_OBJECT_ATTRIBUTES circuitAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&circuitAttributes);

    ACXCIRCUIT                      circuit;
    RETURN_NTSTATUS_IF_FAILED_WITH_CLEANUP(AcxCircuitCreate(device, &circuitAttributes, &circuitInit, &circuit));


    KdPrint(("vsmatrix: EvtDriverDeviceAdd finished.\n"));

cleanup:
    AcxCircuitInitFree(circuitInit);

    return status;
}


// ストリーム作成コールバック
NTSTATUS EvtCircuitCreateStream(
    _In_ WDFDEVICE  Device,
    _In_ ACXCIRCUIT Circuit,
    _In_ ACXPIN Pin,
    _In_ PACXSTREAM_INIT StreamInit,
    _In_ ACXDATAFORMAT DataFormat,
    _In_ const GUID* SignalProcessingMode,
    _In_ ACXOBJECTBAG VarArguments
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Circuit);
    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(SignalProcessingMode);
    UNREFERENCED_PARAMETER(VarArguments);

    KdPrint(("vsmatrix: EvtCircuitCreateStream called.\n"));

    NTSTATUS status = STATUS_SUCCESS;
    auto devCtx = GetDeviceContext(Device);
    ASSERT(devCtx != nullptr);

    // ストリームコールバック設定
    ACX_STREAM_CALLBACKS streamCallbacks;
    ACX_STREAM_CALLBACKS_INIT(&streamCallbacks);

    streamCallbacks.EvtAcxStreamPrepareHardware = EvtStreamPrepareHardware;
    streamCallbacks.EvtAcxStreamRun = EvtStreamRun;
    streamCallbacks.EvtAcxStreamPause = EvtStreamPause;
    
    RETURN_NTSTATUS_IF_FAILED(AcxStreamInitAssignAcxStreamCallbacks(StreamInit, &streamCallbacks));

    // RTストリームコールバック設定
    ACX_RT_STREAM_CALLBACKS         rtCallbacks;
    ACX_RT_STREAM_CALLBACKS_INIT(&rtCallbacks);

    rtCallbacks.EvtAcxStreamGetHwLatency = EvtStreamGetHwLatency;
    rtCallbacks.EvtAcxStreamGetCapturePacket = EvtStreamGetCapturePacket;
    rtCallbacks.EvtAcxStreamGetCurrentPacket = EvtStreamGetCurrentPacket;

    RETURN_NTSTATUS_IF_FAILED(AcxStreamInitAssignAcxRtStreamCallbacks(StreamInit, &rtCallbacks));

    // ストリーム作成
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, STREAM_CONTEXT);

    ACXSTREAM stream;
    RETURN_NTSTATUS_IF_FAILED(AcxRtStreamCreate(Device, Circuit, &attributes, &StreamInit, &stream));

    // パケットバッファアロケート
    auto DeviceDriverTag = (ULONG)'CduA'; // what is CduA???
    auto packet = (PACX_RTPACKET)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ACX_RTPACKET), DeviceDriverTag);
    if (packet == nullptr) {
        KdPrint(("vsmatrix: malloc 1 fails for a packet.\n"));
        return STATUS_NO_MEMORY;
    }

    ACX_RTPACKET_INIT(packet);
    auto packetBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, 512 * sizeof(SHORT), DeviceDriverTag);
    if (packetBuffer == nullptr) {
        KdPrint(("vsmatrix: malloc 2 fails for a packet.\n"));
        // memory leakage: must fix later.
        return STATUS_NO_MEMORY;
    }

    // パケットバッファに波形書き込み
    for (ULONG i = 0; i < 512; i++) {
        // 矩形波の値（最大振幅の50%）
        ((SHORT*)packetBuffer)[i] = i < 256 ? 16384 : -16384;
    }

    auto pMdl = IoAllocateMdl(packetBuffer, 512 * sizeof(SHORT), FALSE, TRUE, NULL);
    if (pMdl == nullptr) {
        KdPrint(("vsmatrix: malloc 3 fails for a packet.\n"));
        // memory leakage: must fix later.
        return STATUS_NO_MEMORY;
    }

    MmBuildMdlForNonPagedPool(pMdl);

    WDF_MEMORY_DESCRIPTOR_INIT_MDL(&packet->RtPacketBuffer, pMdl, 512 * sizeof(SHORT));

    packet->RtPacketSize = 512 * sizeof(SHORT);
    packet->RtPacketOffset = 0;

    // ストリームコンテキスト設定
    auto streamCtx = GetStreamContext(stream);
    ASSERT(streamCtx);
    streamCtx->DataFormat = DataFormat;
    streamCtx->Running = FALSE;
    streamCtx->Packet = packet;

    KdPrint(("vsmatrix: EvtCircuitCreateStream finished.\n"));

    return STATUS_SUCCESS;
}

NTSTATUS
EvtStreamGetHwLatency(
    _In_ ACXSTREAM Stream,
    _Out_ ULONG* FifoSize,
    _Out_ ULONG* Delay
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Stream);
    KdPrint(("vsmatrix: event EvtStreamGetHwLatency raised.\n"));

    *FifoSize = 512 * sizeof(SHORT);
    *Delay = 0;
    return STATUS_SUCCESS;
}


NTSTATUS
EvtStreamGetCapturePacket(
    _In_ ACXSTREAM          Stream,
    _Out_ ULONG* LastCapturePacket,
    _Out_ ULONGLONG* QPCPacketStart,
    _Out_ BOOLEAN* MoreData
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Stream);
    KdPrint(("vsmatrix: event EvtStreamGetCapturePacket raised.\n"));

    *LastCapturePacket = 0;
    *QPCPacketStart = 0;
    *MoreData = true;
    return STATUS_SUCCESS;
}

NTSTATUS
EvtStreamGetCurrentPacket(
    _In_ ACXSTREAM          Stream,
    _Out_ PULONG            CurrentPacket
)
{
    PAGED_CODE();
    KdPrint(("vsmatrix: event EvtStreamGetCurrentPacket raised.\n"));
    UNREFERENCED_PARAMETER(Stream);

    *CurrentPacket = 0;

    return STATUS_SUCCESS;
}

// ストリーム準備
NTSTATUS EvtStreamPrepareHardware(
    _In_ ACXSTREAM Stream
)
{
    KdPrint(("vsmatrix: event EvtStreamPrepareHardware raised.\n"));

    auto streamContext = GetStreamContext(Stream);
    ASSERT(streamContext);

    streamContext->Running = FALSE;

    return STATUS_SUCCESS;
}

// ストリーム開始
NTSTATUS EvtStreamRun(
    _In_ ACXSTREAM Stream
)
{
    KdPrint(("vsmatrix: event EvtStreamRun raised.\n"));

    auto streamContext = GetStreamContext(Stream);
    ASSERT(streamContext);

    streamContext->Running = TRUE;


    return STATUS_SUCCESS;
}

// ストリーム一時停止
NTSTATUS EvtStreamPause(
    _In_ ACXSTREAM Stream
)
{
    KdPrint(("vsmatrix: event EvtStreamPause raised.\n"));

    auto streamContext = GetStreamContext(Stream);
    ASSERT(streamContext);

    streamContext->Running = FALSE;

    return STATUS_SUCCESS;
}
