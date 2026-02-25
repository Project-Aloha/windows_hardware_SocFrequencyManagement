/*
 * SocFrequencyManagement Windows prototype implementation
 * - Hardcoded LUT values taken from sm8150.dtsi (operating-points)
 * - IOCTL-based interface to set/get perf-state index
 * - No real hardware MMIO/ACPI writes (stubs provided)
 */

#include <ntddk.h>
#include <wdf.h>
#include "../include/SocFrequencyManagement.h"

// Helper: clamp index
static unsigned int clamp_index(unsigned int idx, size_t max)
{
    if (idx >= max)
        return (unsigned int)(max - 1);
    return idx;
}

/* Stub: perform hardware perf-state write (NO-OP prototype)
 * Write the perf_state index into the reg_perf_state register (offset 0x920)
 * Domain maps: 
               CPU==0-3 -> mmio[0]
               CPU==4-6 -> mmio[1]
               CPU==7   -> mmio[2]
*/
static NTSTATUS hw_set_perf_state(WDFDEVICE Device, UINT32 Domain, UINT32 Index)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    PVOID base = NULL;
    ULONG *reg;
    const ULONG reg_perf_state_off = 0x920;

    // Only CPU7 (value == 2) is supported; map to MmioBase[2]
    if (Domain == 2) {
        base = devCtx->MmioBase[2];
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    if (base == NULL) {
        KdPrint(("SocFrequencyManagement: hw_set_perf_state domain=%u no MMIO mapped\n", Domain));
        return STATUS_DEVICE_NOT_READY;
    }

    reg = (ULONG *)((PUCHAR)base + reg_perf_state_off);
    WRITE_REGISTER_ULONG(reg, (ULONG)Index);
    KdPrint(("SocFrequencyManagement: wrote perf_state domain=%u index=%u reg=%p\n", Domain, Index, reg));

    return STATUS_SUCCESS;
}

// Exported wrapper so other translation units (Device.c) can set perf-state
NTSTATUS QcomSetPerfState(_In_ WDFDEVICE Device, _In_ UINT32 Domain, _In_ UINT32 Index)
{
    return hw_set_perf_state(Device, Domain, Index);
}

// Read current perf_state index from given domain MMIO
static NTSTATUS read_perf_state_index(WDFDEVICE Device, UINT32 Domain, UINT32 *Index)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    PVOID base = NULL;
    const ULONG reg_perf_state_off = 0x920;

    if (!Index)
        return STATUS_INVALID_PARAMETER;

    if (Domain == 0) base = devCtx->MmioBase[0];
    else if (Domain == 1) base = devCtx->MmioBase[1];
    else if (Domain == 2) base = devCtx->MmioBase[2];
    else return STATUS_INVALID_PARAMETER;

    if (base == NULL)
        return STATUS_DEVICE_NOT_READY;

    {
        ULONG *reg = (ULONG *)((PUCHAR)base + reg_perf_state_off);
        ULONG val = READ_REGISTER_ULONG(reg);
        *Index = (UINT32)val;
        return STATUS_SUCCESS;
    }
}

// Choose the best index in lut_cpu7_khz closest to target_khz
static unsigned int find_closest_index_in_cpu7(unsigned int target_khz)
{
    unsigned int best = 0;
    unsigned int i;
    unsigned int best_diff = (unsigned int)-1;

    for (i = 0; i < lut_cpu7_count; i++) {
        unsigned int val = lut_cpu7_khz[i];
        unsigned int diff = (val > target_khz) ? (val - target_khz) : (target_khz - val);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }

    return best;
}

// Adjust Domain2 based on current Domain1 perf_state frequencies
NTSTATUS QcomAdjustDomain2BasedOn1(_In_ WDFDEVICE Device)
{
    UINT32 idx1 = 0;
    NTSTATUS s1;

    // Only depend on Domain1 (mid-cluster)
    s1 = read_perf_state_index(Device, 1, &idx1);
    if (!NT_SUCCESS(s1)) {
        KdPrint(("SocFrequencyManagement: cannot read domain1 perf_state (s1=0x%08x)\n", s1));
        return STATUS_UNSUCCESSFUL;
    }

    idx1 = clamp_index(idx1, lut_cpu4_count);
    unsigned int freq1 = lut_cpu4_khz[idx1];

    // If domain1 is at its max OPP, set Domain2 to max as well
    unsigned int new_idx;
    if (idx1 == (UINT32)(lut_cpu4_count - 1)) {
        new_idx = (unsigned int)(lut_cpu7_count - 1);
    } else {
        new_idx = find_closest_index_in_cpu7(freq1);
    }

    // Update device context and write hardware
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    devCtx->CurrentIndexDomain2 = new_idx;

    NTSTATUS s = hw_set_perf_state(Device, 2, new_idx);
    if (!NT_SUCCESS(s))
        KdPrint(("SocFrequencyManagement: failed to write Domain2 idx=%u status=0x%08x\n", new_idx, s));
    else
        KdPrint(("SocFrequencyManagement: adjusted Domain2 to idx=%u (~%u kHz) based on domain1=%u kHz\n",
                 new_idx, lut_cpu7_khz[new_idx], freq1));

    return s;
}

// Timer callback: parent object is the device
VOID
QcomPeriodicTimerFunc(_In_ WDFTIMER Timer)
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    if (device == NULL)
        return;

    // Call adjust function; ignore return - logs inside function
    QcomAdjustDomain2BasedOn1(device);
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
