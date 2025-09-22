#include "driver.h"

// ドライバエントリーポイント
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    // デバッグ出力
    KdPrint(("VsMatrix: DriverEntry called\n"));

    // WDFドライバ設定初期化
    WDF_DRIVER_CONFIG_INIT(&config, VSMEvtDeviceAdd);

    // ドライバオブジェクト属性設定
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VSMEvtDriverContextCleanup;

    // WDFドライバオブジェクト作成
    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfDriverCreate failed: 0x%x\n", status));
        return status;
    }

    KdPrint(("VsMatrix: Driver initialized successfully\n"));
    return status;
}

// デバイス追加時のコールバック
NTSTATUS VSMEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    KdPrint(("VsMatrix: VsmatrixEvtDeviceAdd called\n"));

    // オーディオデバイス作成
    status = CreateAudioDevice(Driver, DeviceInit);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: CreateAudioDevice failed: 0x%x\n", status));
        return status;
    }

    return status;
}

// オーディオデバイス作成
NTSTATUS CreateAudioDevice(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit)
{
    NTSTATUS status;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;

    UNREFERENCED_PARAMETER(driver);

    /*
    // デバイス名設定
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, VSM_DEVICE_NAME);

    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfDeviceInitAssignName failed: 0x%x\n", status));
        return status;
    }
    */

    // デバイスコンテキスト属性設定
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    // デバイス作成
    status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfDeviceCreate failed: 0x%x\n", status));
        return status;
    }

    // デバイスコンテキスト取得
    deviceContext = GetDeviceContext(device);
    deviceContext->Device = device;
    deviceContext->SinePhase = 0;
    deviceContext->BufferSize = BUFFER_SIZE;

    // スピンロック初期化
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &deviceContext->BufferLock);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfSpinLockCreate failed: 0x%x\n", status));
        return status;
    }

    // オーディオバッファ割り当て
    deviceContext->AudioBuffer = (SHORT*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        deviceContext->BufferSize,
        'VsAu');
    if (deviceContext->AudioBuffer == NULL) {
        KdPrint(("VsMatrix: Failed to allocate audio buffer\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // バッファを初期化（無音）
    RtlZeroMemory(deviceContext->AudioBuffer, deviceContext->BufferSize);

    // IOキュー設定
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoRead = VSMEvtIoRead;
    queueConfig.EvtIoDeviceControl = VSMEvtIoDeviceControl;

    // IOキュー作成
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfIoQueueCreate failed: 0x%x\n", status));
        if (deviceContext->AudioBuffer) {
            ExFreePoolWithTag(deviceContext->AudioBuffer, 'VsAu');
        }
        return status;
    }

    // オーディオデバイスインターフェースの登録
    // KSCATEGORY_AUDIO GUIDの定義
    // オーディオデバイスカテゴリGUID (手動定義)
    static const GUID KSCATEGORY_AUDIO_GUID = { 0x6994AD04, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };

    // デバイス作成後にインターフェース登録
    status = WdfDeviceCreateDeviceInterface(device, &KSCATEGORY_AUDIO_GUID, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfDeviceCreateDeviceInterface failed: 0x%x\n", status));
        if (deviceContext->AudioBuffer) {
            ExFreePoolWithTag(deviceContext->AudioBuffer, 'VsAu');
        }
        return status;
    }

    KdPrint(("VsMatrix: Audio device created successfully\n"));
    return STATUS_SUCCESS;
}

// 読み取りリクエスト処理
VOID VSMEvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    NTSTATUS status;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    PVOID buffer;
    size_t bufferLength;

    device = WdfIoQueueGetDevice(Queue);
    deviceContext = GetDeviceContext(device);

    KdPrint(("VsMatrix: Read request, Length = %Iu\n", Length));

    // 出力バッファ取得
    status = WdfRequestRetrieveOutputBuffer(Request, Length, &buffer, &bufferLength);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VsMatrix: WdfRequestRetrieveOutputBuffer failed: 0x%x\n", status));
        WdfRequestComplete(Request, status);
        return;
    }

    // サイン波生成
    GenerateSineWave(deviceContext, (PUCHAR)buffer, (ULONG)bufferLength);

    // リクエスト完了
    WdfRequestSetInformation(Request, bufferLength);
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

// デバイス制御リクエスト処理
VOID VSMEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
    size_t OutputBufferLength, size_t InputBufferLength,
    ULONG IoControlCode)
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrint(("VsMatrix: DeviceControl, IoControlCode = 0x%x\n", IoControlCode));

    // 基本的なIOCTLのみサポート
    switch (IoControlCode) {
    case 0x2f0003:  // IOCTL_KS_PROPERTY
    {
        KdPrint(("VsMatrix: IOCTL_KS_PROPERTY received\n"));
        status = HandleKSProperty(Request, InputBufferLength, OutputBufferLength);
        break;
    }
    default:
        KdPrint(("VsMatrix: Unsupported IOCTL: 0x%x\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, status);
}

// 方形波生成関数（シンプル版）
VOID GenerateSineWave(PDEVICE_CONTEXT deviceContext, PUCHAR buffer, ULONG length)
{
    ULONG samples = length / BLOCK_ALIGN;
    SHORT* shortBuffer = (SHORT*)buffer;
    ULONG i;
    ULONG samplesPerCycle = SAMPLE_RATE / SINE_FREQUENCY;  // 440Hz用の1周期のサンプル数
    ULONG phaseInCycle;
    SHORT sampleValue;

    WdfSpinLockAcquire(deviceContext->BufferLock);

    for (i = 0; i < samples; i++) {
        // 1周期内での位相を計算
        phaseInCycle = deviceContext->SinePhase % samplesPerCycle;

        // 方形波生成（前半が+、後半が-）
        if (phaseInCycle < samplesPerCycle / 2) {
            sampleValue = 16383;   // +半分の振幅（少し控えめに）
        }
        else {
            sampleValue = -16383;  // -半分の振幅
        }

        // ステレオ両チャンネルに同じ値を設定
        shortBuffer[i * 2] = sampleValue;       // Left channel
        shortBuffer[i * 2 + 1] = sampleValue;   // Right channel

        // 位相を進める
        deviceContext->SinePhase++;
        if (deviceContext->SinePhase >= SAMPLE_RATE) {
            deviceContext->SinePhase = 0;  // 1秒でリセット
        }
    }

    WdfSpinLockRelease(deviceContext->BufferLock);

    KdPrint(("VsMatrix: Generated %u samples of square wave\n", samples));
}

// ドライバコンテキストクリーンアップ
VOID VSMEvtDriverContextCleanup(WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrint(("VsMatrix: Driver context cleanup\n"));
}

// KSプロパティハンドラー
NTSTATUS HandleKSProperty(WDFREQUEST Request, size_t
    InputBufferLength, size_t OutputBufferLength)
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    WDF_REQUEST_PARAMETERS params;
    PUCHAR inputBuffer;
    ULONG outputValue = 0; // 0 is ad-hock

    KdPrint(("VsMatrix: HandleKSProperty called, InputLen=%Iu, OutputLen=%Iu\n",
        InputBufferLength, OutputBufferLength));

    // リクエストパラメータ取得
    WDF_REQUEST_PARAMETERS_INIT(&params);
    WdfRequestGetParameters(Request, &params);

    // METHOD_NEITHERの場合の入力バッファーアクセス
    inputBuffer = (PUCHAR)params.Parameters.DeviceIoControl.Type3InputBuffer;

    if (inputBuffer == NULL) {
        KdPrint(("VsMatrix: Input buffer is NULL\n"));
        return STATUS_INVALID_PARAMETER;
    }

    __try {
        // KSプロパティ構造体の解析（簡略化）
        if (InputBufferLength >= 20) {  // 最小サイズチェック
            ULONG propertyId = *((PULONG)(inputBuffer + 16));  // GUID後のプロパティID

            KdPrint(("VsMatrix: Property ID: %u\n", propertyId));

            switch (propertyId) {
            case KSPROPERTY_PIN_CTYPES:  // ピン数
                outputValue = 1;  // 1つの入力ピンがある
                status = STATUS_SUCCESS;
                KdPrint(("VsMatrix: Returned pin count: 1\n"));
                break;

            case KSPROPERTY_PIN_DATAFLOW:  // データフロー方向
                outputValue = KSPIN_DATAFLOW_IN;  // 入力デバイス
                status = STATUS_SUCCESS;
                KdPrint(("VsMatrix: Returned dataflow: IN\n"));
                break;

            case KSPROPERTY_PIN_COMMUNICATION:  // 通信タイプ
                outputValue = KSPIN_COMMUNICATION_SINK;  // シンク
                status = STATUS_SUCCESS;
                KdPrint(("VsMatrix: Returned communication: SINK\n"));
                break;

            default:
                KdPrint(("VsMatrix: Unsupported property: %u\n", propertyId));
                status = STATUS_NOT_SUPPORTED;
                break;
            }

            // 成功した場合、出力バッファーに値を設定
            if (NT_SUCCESS(status) && OutputBufferLength >= sizeof(ULONG)) {
                // WdfRequestのMethod Neitherハンドリング用
                WDFMEMORY outputMemory;
                PULONG outputBuffer;

                status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);
                if (NT_SUCCESS(status)) {
                    outputBuffer = (PULONG)WdfMemoryGetBuffer(outputMemory, NULL);
                    if (outputBuffer != NULL) {
                        *outputBuffer = outputValue;
                        WdfRequestSetInformation(Request, sizeof(ULONG));
                    }
                    else {
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                }
            }
        }
        else {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        KdPrint(("VsMatrix: Exception occurred while accessing user buffers\n"));
        status = STATUS_INVALID_USER_BUFFER;
    }

    return status;
}
