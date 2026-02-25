#pragma once

#include <ntddk.h>
#include <wdf.h>

// IOCTL definitions
#define IOCTL_QCOM_GET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_QCOM_SET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

// Hardcoded LUTs (frequencies in kHz) from sm8150.dtsi OPP tables
static const unsigned int lut_cpu0_khz[] = {
    300000,   /* 300.0 MHz */
    403200,   /* 403.2 MHz */
    499200,   /* 499.2 MHz */
    576000,   /* 576.0 MHz */
    672000,   /* 672.0 MHz */
    768000,   /* 768.0 MHz */
    844800,   /* 844.8 MHz */
    940800,   /* 940.8 MHz */
   1036800,   /* 1036.8 MHz */
   1113600,   /* 1113.6 MHz */
   1209600,   /* 1209.6 MHz */
   1305600,   /* 1305.6 MHz */
   1382400,   /* 1382.4 MHz */
   1478400,   /* 1478.4 MHz */
   1555200,   /* 1555.2 MHz */
   1632000,   /* 1632.0 MHz */
   1708800,   /* 1708.8 MHz */
   1785600    /* 1785.6 MHz */
};

static const size_t lut_cpu0_count = sizeof(lut_cpu0_khz) / sizeof(lut_cpu0_khz[0]);

static const unsigned int lut_cpu4_khz[] = {
    710400,   /* 710.4 MHz */
    825600,   /* 825.6 MHz */
    940800,   /* 940.8 MHz */
   1056000,   /* 1056.0 MHz */
   1171200,   /* 1171.2 MHz */
   1286400,   /* 1286.4 MHz */
   1401600,   /* 1401.6 MHz */
   1497600,   /* 1497.6 MHz */
   1612800,   /* 1612.8 MHz */
   1708800,   /* 1708.8 MHz */
   1804800,   /* 1804.8 MHz */
   1920000,   /* 1920.0 MHz */
   2016000,   /* 2016.0 MHz */
   2131200,   /* 2131.2 MHz */
   2227200,   /* 2227.2 MHz */
   2323200,   /* 2323.2 MHz */
   2419200    /* 2419.2 MHz */
};
static const size_t lut_cpu4_count = sizeof(lut_cpu4_khz) / sizeof(lut_cpu4_khz[0]);

static const unsigned int lut_cpu7_khz[] = {
    825600,   /* 825.6 MHz */
    940800,   /* 940.8 MHz */
   1056000,   /* 1056.0 MHz */
   1171200,   /* 1171.2 MHz */
   1286400,   /* 1286.4 MHz */
   1401600,   /* 1401.6 MHz */
   1497600,   /* 1497.6 MHz */
   1612800,   /* 1612.8 MHz */
   1708800,   /* 1708.8 MHz */
   1804800,   /* 1804.8 MHz */
   1920000,   /* 1920.0 MHz */
   2016000,   /* 2016.0 MHz */
   2131200,   /* 2131.2 MHz */
   2227200,   /* 2227.2 MHz */
   2323200,   /* 2323.2 MHz */
   2419200,   /* 2419.2 MHz */
   2534400,   /* 2534.4 MHz */
   2649600,   /* 2649.6 MHz */
   2745600,   /* 2745.6 MHz */
   2841600    /* 2841.6 MHz */
};
static const size_t lut_cpu7_count = sizeof(lut_cpu7_khz) / sizeof(lut_cpu7_khz[0]);

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
    // Per-domain per-core DCVS capability (true if per-core perf_state registers are supported)
    BOOLEAN PerCoreDcvs[3];
    // WDF timer for periodic Domain2 adjustment
    WDFTIMER PeriodicTimer;
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
NTSTATUS QcomAdjustDomain2BasedOn1(_In_ WDFDEVICE Device);
