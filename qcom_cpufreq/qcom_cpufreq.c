/*
 * qcom_cpufreq Windows prototype implementation
 * - Hardcoded LUT values taken from sm8150.dtsi (operating-points)
 * - IOCTL-based interface to set/get perf-state index
 * - No real hardware MMIO/ACPI writes (stubs provided)
 */

#include <ntddk.h>
#include <wdf.h>
#include "qcom_cpufreq.h"

// Hardcoded LUTs (frequencies in kHz) from sm8150.dtsi OPP tables
static const unsigned int lut_cpu0_khz[] = {
    300000,403200,499200,576000,672000,768000,844800,940800,
    1036800,1113600,1209600,1305600,1382400,1478400,1555200,1632000,1708800,1785600
};

static const size_t lut_cpu0_count = sizeof(lut_cpu0_khz)/sizeof(lut_cpu0_khz[0]);

static const unsigned int lut_cpu4_khz[] = {
    710400,825600,940800,1056000,1171200,1286400,1401600,1497600,
    1612800,1708800,1804800,1920000,2016000,2131200,2227200,2323200,2419200
};
static const size_t lut_cpu4_count = sizeof(lut_cpu4_khz)/sizeof(lut_cpu4_khz[0]);

static const unsigned int lut_cpu7_khz[] = {
    825600,940800,1056000,1171200,1286400,1401600,1497600,1612800,
    1708800,1804800,1920000,2016000,2131200,2227200,2323200,2419200,
    2534400,2649600,2745600,2841600
};
static const size_t lut_cpu7_count = sizeof(lut_cpu7_khz) / sizeof(lut_cpu7_khz[0]);

// Helper: clamp index
static unsigned int clamp_index(unsigned int idx, size_t max)
{
    if (idx >= max)
        return (unsigned int)(max - 1);
    return idx;
}

// Stub: perform hardware perf-state write (NO-OP prototype)
// Write the perf_state index into the reg_perf_state register (offset 0x920)
// Domain maps: Domain==0 -> mmio[0], Domain==4 -> mmio[1], Domain==7 -> mmio[2]
static NTSTATUS hw_set_perf_state(WDFDEVICE Device, UINT32 Domain, UINT32 Index)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    PVOID base = NULL;
    ULONG *reg;
    const ULONG reg_perf_state_off = 0x920;

    // Only Domain2 (value == 2) is supported; map to MmioBase[2]
    if (Domain == 2) {
        base = devCtx->MmioBase[2];
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    if (base == NULL) {
        KdPrint(("qcom_cpufreq: hw_set_perf_state domain=%u no MMIO mapped\n", Domain));
        return STATUS_DEVICE_NOT_READY;
    }

    reg = (ULONG *)((PUCHAR)base + reg_perf_state_off);
    WRITE_REGISTER_ULONG(reg, (ULONG)Index);
    KdPrint(("qcom_cpufreq: wrote perf_state domain=%u index=%u reg=%p\n", Domain, Index, reg));

    return STATUS_SUCCESS;
}

// Exported wrapper so other translation units (Device.c) can set perf-state
NTSTATUS QcomSetPerfState(_In_ WDFDEVICE Device, _In_ UINT32 Domain, _In_ UINT32 Index)
{
    return hw_set_perf_state(Device, Domain, Index);
}

NTSTATUS QcomEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);

    UNREFERENCED_PARAMETER(OutputBufferLength);

    switch (IoControlCode) {
    case IOCTL_QCOM_GET_FREQ:
        {
            // Expect input: Domain (UINT32), output: UINT32 frequency_khz
            PVOID inBuf = NULL;
            size_t inLen = 0;
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT32), &inBuf, &inLen);
            if (!NT_SUCCESS(status)) break;
            UINT32 domain = *(UINT32 *)inBuf;

            if (domain != 2) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            UINT32 freq = 0;
            unsigned int idx = devCtx->CurrentIndexDomain2;
            idx = clamp_index(idx, lut_cpu7_count);
            freq = lut_cpu7_khz[idx];

            // Return freq as output buffer
            PVOID outBuf = NULL;
            size_t outLen = 0;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UINT32), &outBuf, &outLen);
            if (!NT_SUCCESS(status)) break;
            *(UINT32 *)outBuf = freq;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(UINT32));
            return STATUS_SUCCESS;
        }

    case IOCTL_QCOM_SET_FREQ:
        {
            PVOID inBuf = NULL;
            size_t inLen = 0;
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(QCOM_SET_FREQ_IN), &inBuf, &inLen);
            if (!NT_SUCCESS(status)) break;

            QCOM_SET_FREQ_IN *in = (QCOM_SET_FREQ_IN *)inBuf;

            if (in->Domain != 2) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            devCtx->CurrentIndexDomain2 = clamp_index(in->Index, lut_cpu7_count);

            // Call hardware stub (no-op)
            status = hw_set_perf_state(device, in->Domain, in->Index);
            WdfRequestComplete(Request, status);
            return status;
        }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, status);
    return status;
}
