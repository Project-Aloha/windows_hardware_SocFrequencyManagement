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

    // Map domain to MMIO base: domain indices 0/1/2 map to MmioBase[0..2]
    if (Domain == 0) {
        base = devCtx->MmioBase[0];
    }
    else if (Domain == 1) {
        base = devCtx->MmioBase[1];
    }
    else if (Domain == 2) {
        base = devCtx->MmioBase[2];
    }
    else 
        return STATUS_INVALID_PARAMETER;

    if (base == NULL) {
        KdPrint(("SocFrequencyManagement: hw_set_perf_state domain=%u no MMIO mapped\n", Domain));
        return STATUS_DEVICE_NOT_READY;
    }

    // If per-core DCVS is enabled for this domain, write the same index
    // into successive per-core perf_state registers (offset + 4 per core)
    {
        int core_count = 1;
        int i;

        if (devCtx->PerCoreDcvs[Domain]) {
            if (Domain == 0) {
                core_count = 4;    // CPUs 0-3
            }
            else if (Domain == 1) {
                core_count = 3;    // CPUs 4-6
            }
            else if (Domain == 2) {
                core_count = 1;    // CPU7
            }
        }

        for (i = 0; i < core_count; i++) {
            reg = (ULONG *)((PUCHAR)base + REG_PERF_STATE + i * sizeof(ULONG));
            WRITE_REGISTER_ULONG(reg, (ULONG)Index);
        }

        KdPrint(("SocFrequencyManagement: wrote perf_state domain=%u index=%u cores=%d base=%p\n",
                 Domain, Index, core_count, base));
    }

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

    if (!Index) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Domain == 0) {
        base = devCtx->MmioBase[0];
    }
    else if (Domain == 1) {
        base = devCtx->MmioBase[1];
    }
    else if (Domain == 2) {
        base = devCtx->MmioBase[2];
    }
    else {
        return STATUS_INVALID_PARAMETER;
    }

    if (base == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    {
        ULONG *reg = (ULONG *)((PUCHAR)base + REG_PERF_STATE);
        ULONG val = READ_REGISTER_ULONG(reg);
        *Index = (UINT32)val;
        return STATUS_SUCCESS;
    }
}

// Parse LUT from hardware registers for a specific domain
// Follows Linux kernel qcom_cpufreq_hw_read_lut() logic
NTSTATUS QcomParseLut(_In_ WDFDEVICE Device, _In_ UINT32 Domain)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    PVOID base = NULL;
    UINT32 i;
    UINT32 prevFreq = 0;
    UINT32 validCount = 0;

    if (Domain > 2) {
        return STATUS_INVALID_PARAMETER;
    }

    base = devCtx->MmioBase[Domain];
    if (base == NULL) {
        KdPrint(("QcomParseLut: domain %u not mapped\n", Domain));
        devCtx->LutValid[Domain] = FALSE;
        return STATUS_DEVICE_NOT_READY;
    }

    // Initialize all entries as invalid
    for (i = 0; i < LUT_MAX_ENTRIES; i++) {
        devCtx->ParsedLut[Domain][i].FreqKhz = 0;
        devCtx->ParsedLut[Domain][i].CoreCount = 0;
        devCtx->ParsedLut[Domain][i].Valid = FALSE;
    }

    for (i = 0; i < LUT_MAX_ENTRIES; i++) {
        ULONG dataFreq, dataVolt;
        ULONG src, lval, coreCount;
        UINT32 freqKhz;

        // Read frequency LUT entry
        dataFreq = READ_REGISTER_ULONG((ULONG *)((PUCHAR)base + REG_FREQ_LUT + i * LUT_ROW_SIZE));
        // Read voltage LUT entry (for reference, not used for freq calculation)
        dataVolt = READ_REGISTER_ULONG((ULONG *)((PUCHAR)base + REG_VOLT_LUT + i * LUT_ROW_SIZE));
        UNREFERENCED_PARAMETER(dataVolt);

        src = (dataFreq & LUT_SRC_MASK) >> LUT_SRC_SHIFT;
        lval = dataFreq & LUT_L_VAL_MASK;
        coreCount = (dataFreq & LUT_CORE_COUNT_MASK) >> LUT_CORE_COUNT_SHIFT;

        // Calculate frequency
        // If src is set, freq = xo_rate * lval
        // Otherwise use a fixed backup rate (we use hardcoded fallback)
        if (src) {
            freqKhz = (UINT32)((XO_RATE_HZ * lval) / 1000);
        } else {
            // Fallback: this shouldn't happen often on SM8150
            freqKhz = (UINT32)(XO_RATE_HZ / 1000);
        }

        // Skip turbo indicator entries and duplicate frequencies
        if (coreCount == LUT_TURBO_IND) {
            devCtx->ParsedLut[Domain][i].FreqKhz = freqKhz;
            devCtx->ParsedLut[Domain][i].CoreCount = coreCount;
            devCtx->ParsedLut[Domain][i].Valid = FALSE; // Mark as invalid for regular use
            continue;
        }

        // Check for end of table: two consecutive same frequencies
        if (i > 0 && prevFreq == freqKhz) {
            // Check if previous was turbo indicator - if so, mark it valid as boost freq
            if (devCtx->ParsedLut[Domain][i - 1].Valid == FALSE &&
                devCtx->ParsedLut[Domain][i - 1].FreqKhz == prevFreq) {
                devCtx->ParsedLut[Domain][i - 1].Valid = TRUE;
                validCount++;
                KdPrint(("QcomParseLut: domain %u idx %u boost freq %u kHz\n", 
                         Domain, i - 1, prevFreq));
            }
            break; // End of LUT
        }

        if (freqKhz != prevFreq) {
            devCtx->ParsedLut[Domain][i].FreqKhz = freqKhz;
            devCtx->ParsedLut[Domain][i].CoreCount = coreCount;
            devCtx->ParsedLut[Domain][i].Valid = TRUE;
            validCount++;

            KdPrint(("QcomParseLut: domain %u idx %u freq=%u kHz cores=%u\n", 
                     Domain, i, freqKhz, coreCount));
        }

        prevFreq = freqKhz;
    }

    devCtx->LutCount[Domain] = validCount;
    devCtx->LutValid[Domain] = (validCount > 0);

    KdPrint(("QcomParseLut: domain %u total valid entries=%u\n", Domain, validCount));

    return (validCount > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// Find closest index in domain's parsed LUT for a target frequency
// Returns index in LUT, or fallback_index if LUT not valid
static unsigned int find_closest_index_in_parsed_lut(
    PDEVICE_CONTEXT devCtx,
    UINT32 Domain,
    unsigned int target_khz,
    unsigned int fallback_index)
{
    unsigned int best = 0;
    unsigned int best_diff = (unsigned int)-1;
    unsigned int i;
    BOOLEAN found = FALSE;

    if (Domain > 2 || !devCtx->LutValid[Domain]) {
        return fallback_index;
    }

    for (i = 0; i < LUT_MAX_ENTRIES; i++) {
        if (devCtx->ParsedLut[Domain][i].Valid) {
            unsigned int freq = devCtx->ParsedLut[Domain][i].FreqKhz;
            unsigned int diff = (freq > target_khz) ? (freq - target_khz) : (target_khz - freq);
            if (diff < best_diff) {
                best_diff = diff;
                best = i;
                found = TRUE;
            }
        }
    }

    return found ? best : fallback_index;
}

// Get frequency for a given index in domain's LUT
// Uses parsed LUT if available, otherwise falls back to hardcoded
static unsigned int get_freq_for_index(PDEVICE_CONTEXT devCtx, UINT32 Domain, unsigned int index)
{
    // Try parsed LUT first
    if (Domain < 3 && devCtx->LutValid[Domain]) {
        if (index < LUT_MAX_ENTRIES && devCtx->ParsedLut[Domain][index].Valid) {
            return devCtx->ParsedLut[Domain][index].FreqKhz;
        }
        // If index is out of range, find valid entry
        for (unsigned int i = 0; i < LUT_MAX_ENTRIES; i++) {
            if (devCtx->ParsedLut[Domain][i].Valid) {
                // Return last valid entry if index too high
                if (i >= index || i == LUT_MAX_ENTRIES - 1) {
                    return devCtx->ParsedLut[Domain][i].FreqKhz;
                }
            }
        }
    }

    // Fallback to hardcoded LUTs
    if (Domain == 0) {
        index = clamp_index(index, lut_cpu0_count);
        return lut_cpu0_khz[index];
    } else if (Domain == 1) {
        index = clamp_index(index, lut_cpu4_count);
        return lut_cpu4_khz[index];
    } else if (Domain == 2) {
        index = clamp_index(index, lut_cpu7_count);
        return lut_cpu7_khz[index];
    }

    return 0;
}

// Get max index for a domain (uses parsed LUT if valid)
static unsigned int get_max_index(PDEVICE_CONTEXT devCtx, UINT32 Domain)
{
    if (Domain < 3 && devCtx->LutValid[Domain]) {
        // Find last valid entry
        for (int i = LUT_MAX_ENTRIES - 1; i >= 0; i--) {
            if (devCtx->ParsedLut[Domain][i].Valid) {
                return (unsigned int)i;
            }
        }
    }

    // Fallback to hardcoded
    if (Domain == 0) {
        return (unsigned int)(lut_cpu0_count - 1);
    } else if (Domain == 1) {
        return (unsigned int)(lut_cpu4_count - 1);
    } else if (Domain == 2) {
        return (unsigned int)(lut_cpu7_count - 1);
    }
    return 0;
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
// Uses parsed LUT if available, otherwise falls back to hardcoded LUT
NTSTATUS QcomAdjustDomain2BasedOn1(_In_ WDFDEVICE Device)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    UINT32 idx1 = 0;
    NTSTATUS s1;
    unsigned int freq1, new_idx;
    unsigned int max_idx1, max_idx2;

    // Only depend on Domain1 (mid-cluster)
    s1 = read_perf_state_index(Device, 1, &idx1);
    if (!NT_SUCCESS(s1)) {
        KdPrint(("SocFrequencyManagement: cannot read domain1 perf_state (s1=0x%08x)\n", s1));
        return STATUS_UNSUCCESSFUL;
    }

    // Get max indices for domains 1 and 2
    max_idx1 = get_max_index(devCtx, 1);
    max_idx2 = get_max_index(devCtx, 2);

    // Clamp idx1 to valid range
    if (idx1 > max_idx1) idx1 = max_idx1;

    // Get frequency at idx1
    freq1 = get_freq_for_index(devCtx, 1, idx1);

    // If domain1 is at its max OPP, set Domain2 to max as well
    if (idx1 == (UINT32)max_idx1) {
        new_idx = max_idx2;
    } else {
        // Find closest frequency in Domain2 LUT
        // First try parsed LUT with fallback
        unsigned int fallback = find_closest_index_in_cpu7(freq1);
        new_idx = find_closest_index_in_parsed_lut(devCtx, 2, freq1, fallback);
    }

    // Update device context and write hardware
    devCtx->CurrentIndexDomain2 = new_idx;

    NTSTATUS s = hw_set_perf_state(Device, 2, new_idx);
    if (!NT_SUCCESS(s)) {
        KdPrint(("SocFrequencyManagement: failed to write Domain2 idx=%u status=0x%08x\n", new_idx, s));
    } else {
        unsigned int freq2 = get_freq_for_index(devCtx, 2, new_idx);
        KdPrint(("SocFrequencyManagement: adjusted Domain2 to idx=%u (~%u kHz) based on domain1=%u kHz%s\n",
                 new_idx, freq2, freq1, 
                 devCtx->LutValid[2] ? " [parsed LUT]" : " [hardcoded]"));
    }

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

            unsigned int idx = devCtx->CurrentIndexDomain2;
            UINT32 freq = get_freq_for_index(devCtx, 2, idx);

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

            // Clamp index to max valid index for domain 2
            unsigned int max_idx = get_max_index(devCtx, 2);
            devCtx->CurrentIndexDomain2 = (in->Index > max_idx) ? max_idx : in->Index;

            // Write to hardware
            status = hw_set_perf_state(device, in->Domain, devCtx->CurrentIndexDomain2);
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
