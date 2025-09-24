

#include <ntddk.h>
#include <wdf.h>
#include <acx.h>
#include <devguid.h>


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
} STREAM_CONTEXT, * PSTREAM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STREAM_CONTEXT, GetStreamContext)

// 前方宣言
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_ACX_CIRCUIT_CREATE_STREAM EvtCircuitCreateStream;
EVT_ACX_STREAM_PREPARE_HARDWARE EvtStreamPrepareHardware;
EVT_ACX_STREAM_RUN EvtStreamRun;
EVT_ACX_STREAM_PAUSE EvtStreamPause;
EVT_WDF_TIMER EvtTimerFunc;

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

/*
    // タイマー作成（データ生成用）
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, EvtTimerFunc, 10); // 10ms間隔

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;

    status = WdfTimerCreate(&timerConfig, &attributes, &deviceContext->Timer);
*/

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

    // ストリーム作成
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, STREAM_CONTEXT);

    ACXSTREAM stream;
    RETURN_NTSTATUS_IF_FAILED(AcxRtStreamCreate(Device, Circuit, &attributes, &StreamInit, &stream));

    auto streamCtx = GetStreamContext(stream);
    ASSERT(streamCtx);
    streamCtx->DataFormat = DataFormat;
    streamCtx->Running = FALSE;

    KdPrint(("vsmatrix: EvtCircuitCreateStream finished.\n"));

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

// タイマーコールバック - 440Hz矩形波生成
VOID EvtTimerFunc(
    _In_ WDFTIMER Timer
)
{
    UNREFERENCED_PARAMETER(Timer);
    /*
    WDFDEVICE device = WdfTimerGetParentObject(Timer);
    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);

    if (deviceContext->Stream == NULL) {
        return;
    }

    // RTパケット取得
    PACX_RT_PACKET packet = nullptr;
    NTSTATUS status = AcxStreamGetRtPacket(
        deviceContext->Stream,
        &packet,
        0
    );

    if (!NT_SUCCESS(status) || packet == nullptr) {
        return;
    }

    // 16ビット PCM として処理
    SHORT* buffer = (SHORT*)packet->Buffer;
    ULONG sampleCount = packet->BufferSize / sizeof(SHORT);

    // 440Hz矩形波生成
    // サンプルレート48000Hzで440Hzを生成 → 約109サンプルで1周期
    ULONG samplesPerPeriod = deviceContext->SampleRate / 440;

    for (ULONG i = 0; i < sampleCount; i++) {
        // 矩形波の値（最大振幅の50%）
        buffer[i] = deviceContext->HighState ? 16384 : -16384;

        deviceContext->CurrentSample++;

        // 半周期ごとに状態を切り替え
        if (deviceContext->CurrentSample >= samplesPerPeriod / 2) {
            deviceContext->HighState = !deviceContext->HighState;
            deviceContext->CurrentSample = 0;
        }
    }

    // パケット返却
    AcxStreamReturnRtPacket(deviceContext->Stream, packet);
    */
}

