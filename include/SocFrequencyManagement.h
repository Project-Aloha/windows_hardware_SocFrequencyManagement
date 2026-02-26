#pragma once

#include <ntddk.h>
#include <wdf.h>

// IOCTL definitions
#define IOCTL_QCOM_GET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_QCOM_SET_FREQ CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

// LUT register offsets (qcom_soc_data for SM8150)
#define REG_ENABLE           0x0
#define REG_DCVS_CTRL        0xbc
#define REG_FREQ_LUT         0x110
#define REG_VOLT_LUT         0x114
#define REG_PERF_STATE       0x920
#define LUT_ROW_SIZE         32       // bytes between LUT entries
#define LUT_MAX_ENTRIES      40

// LUT field masks
#define LUT_SRC_MASK         0xC0000000  // bits 31:30
#define LUT_SRC_SHIFT        30
#define LUT_L_VAL_MASK       0x000000FF  // bits 7:0
#define LUT_CORE_COUNT_MASK  0x00070000  // bits 18:16
#define LUT_CORE_COUNT_SHIFT 16
#define LUT_TURBO_IND        1

// XO clock rate in Hz (19.2 MHz for Qualcomm platforms)
#define XO_RATE_HZ           19200000ULL

// Hardcoded LUTs (frequencies in kHz) from sm8150.dtsi OPP tables - used as fallback
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

// Parsed LUT entry
typedef struct _PARSED_LUT_ENTRY {
    UINT32 FreqKhz;     // Frequency in kHz
    UINT32 CoreCount;   // Number of cores at this OPP
    BOOLEAN Valid;      // Whether this entry is valid
} PARSED_LUT_ENTRY, *PPARSED_LUT_ENTRY;

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
    // Parsed LUT tables for each domain
    PARSED_LUT_ENTRY ParsedLut[3][LUT_MAX_ENTRIES];
    // Number of valid entries in each domain's LUT
    UINT32 LutCount[3];
    // Whether LUT was successfully parsed for each domain
    BOOLEAN LutValid[3];
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

// LUT parsing function - reads frequency LUT from hardware registers
NTSTATUS QcomParseLut(_In_ WDFDEVICE Device, _In_ UINT32 Domain);
