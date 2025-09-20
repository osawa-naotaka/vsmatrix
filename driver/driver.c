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