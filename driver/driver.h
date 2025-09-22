#ifndef _VSMATRIX_H_
#define _VSMATRIX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

// KS �v���p�e�B�萔
#define KSPROPERTY_TYPE_GET                0x00000001
#define KSPROPERTY_TYPE_SET                0x00000002
#define KSPROPERTY_TYPE_TOPOLOGY           0x10000000

// �s���v���p�e�B
#define KSPROPERTY_PIN_CTYPES              0
#define KSPROPERTY_PIN_DATAFLOW            3  
#define KSPROPERTY_PIN_COMMUNICATION       4

// �f�[�^�t���[�萔
#define KSPIN_DATAFLOW_IN                  1
#define KSPIN_DATAFLOW_OUT                 2

// �ʐM�^�C�v�萔  
#define KSPIN_COMMUNICATION_SINK           1
#define KSPIN_COMMUNICATION_SOURCE         2

// �f�o�C�X��
//#define VSM_DEVICE_NAME L"\\Device\\VsmatrixVAIF1"
#define VSM_DEVICE_NAME L"\\Device\\Vsmatrix"

// �I�[�f�B�I�t�H�[�}�b�g��`
#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define CHANNELS 2
#define BYTES_PER_SAMPLE (BITS_PER_SAMPLE / 8)
#define BLOCK_ALIGN (CHANNELS * BYTES_PER_SAMPLE)
#define SINE_FREQUENCY 440
#define BUFFER_SIZE (SAMPLE_RATE * BLOCK_ALIGN)  // 1�b��

// �f�o�C�X�R���e�L�X�g�\����
typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    ULONG SinePhase;        // �T�C���g�̈ʑ�
    SHORT* AudioBuffer;     // �I�[�f�B�I�o�b�t�@
    ULONG BufferSize;       // �o�b�t�@�T�C�Y
    WDFSPINLOCK BufferLock; // �o�b�t�@�A�N�Z�X�p�X�s�����b�N
} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

// �֐��v���g�^�C�v
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VSMEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VSMEvtDriverContextCleanup;
EVT_WDF_IO_QUEUE_IO_READ VSMEvtIoRead;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VSMEvtIoDeviceControl;

// �w���p�[�֐�
VOID GenerateSineWave(PDEVICE_CONTEXT deviceContext, PUCHAR buffer, ULONG length);
NTSTATUS CreateAudioDevice(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit);

NTSTATUS HandleKSProperty(WDFREQUEST Request, size_t
    InputBufferLength, size_t OutputBufferLength);


#ifdef __cplusplus
}
#endif

#endif // _VSMATRIX_H_