#pragma once

#include <ntddk.h>
#include <wdf.h>

// IOCTL definitions
#define IOCTL_QCOM_GET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_QCOM_SET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

// Input/Output structures
typedef struct _QCOM_SET_FREQ_IN {
    UINT32 Domain; // 0 = cpu0 (little), 4 = cpu4 (mid), 7 = cpu7 (big)
    UINT32 Index;  // LUT index
} QCOM_SET_FREQ_IN, *PQCOM_SET_FREQ_IN;

// Device context to keep per-domain current index
typedef struct _DEVICE_CONTEXT {
    // Current index for only Domain2 (third freq-domain)
    UINT32 CurrentIndexDomain2;
    // MMIO bases for three freq domains (only MmioBase[2] used)
    PVOID MmioBase[3];
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS QcomEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    );

// Exported helper to set perf state from other modules (e.g. Device.c)
NTSTATUS QcomSetPerfState(_In_ WDFDEVICE Device, _In_ UINT32 Domain, _In_ UINT32 Index);
