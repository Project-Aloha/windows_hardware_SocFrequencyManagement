# SocFrequencyManagement Driver (Hana Platform)

## Overview
* SocFrequencyManagement(Hana) is a KMDF prototype driver demonstrating control of a Qualcomm SM8150 (Snapdragon 855/860) frequency domain (Domain2 — big cluster / CPU7) from Windows. This repository is a research/proof-of-concept and is not intended for production use.

## Key features
- **Runtime LUT parsing**: the driver reads the hardware frequency LUT from `reg_freq_lut` registers at initialization. If parsing fails, it falls back to a hardcoded LUT derived from the sm8150 OPP table.
- **Domain2 control**: write a perf_state index to the domain register to change the big-cluster frequency.
- **Per-core DCVS**: the driver probes `reg_dcvs_ctrl` and, if supported, writes per-core `perf_state` registers.
- **Automatic adjustment**: a periodic WDF timer reads Domain1 and adjusts Domain2 using a simple mapping policy (see the source for details).
- **User-space interface**: basic IOCTLs allow reading and setting the Domain2 frequency index.

## Important files
- Driver sources: [src/SocFrequencyManagement.c](src/SocFrequencyManagement.c)
- Device initialization and MMIO probe: [src/Device.c](src/Device.c)
- Headers, LUTs and IOCTLs: [include/SocFrequencyManagement.h](include/SocFrequencyManagement.h)
- Visual Studio solution / project: [SocFrequencyManagement.sln](SocFrequencyManagement.sln), [SocFrequencyManagement.vcxproj](SocFrequencyManagement.vcxproj)

## Build
* Requirements: Visual Studio with the Windows Driver Kit (WDK).
  * 1. Open `SocFrequencyManagement.sln` in Visual Studio.
  * 2. Select the desired target platform (ARM64/x64) in configuration manager.
  * 3. Build the solution.

# Quick test

The driver exposes two IOCTLs defined in `include/SocFrequencyManagement.h`:

- `IOCTL_QCOM_GET_FREQ` — get the current Domain2 frequency (kHz) via the driver’s current index
- `IOCTL_QCOM_SET_FREQ` — set Domain2 target index (the driver clamps the index to the LUT range)

Typical workflow: open a handle to the device from user space and call `DeviceIoControl` with the IOCTL above to read or set Domain2 index.

## Limitations
- Proof-of-concept only: physical MMIO addresses and register offsets are hardcoded in the driver; resources are not discovered via ACPI or platform APIs.
- No firmware/ACPI integration: this prototype cannot expose cpu7 frequency as native system P‑states. ACPI changes (exposing _PSS/_PCT/_PSD) or OEM firmware updates are required for native P‑state integration.
- Not production hardened: the driver lacks signing/deployment workflows, exhaustive error handling, and comprehensive concurrency/IRQL audits.

## References
* Linux reference: `/1work/linux/linux/drivers/cpufreq/qcom-cpufreq-hw.c`
