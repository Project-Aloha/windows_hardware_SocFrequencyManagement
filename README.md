# Qualcomm Snapdragon 855 Domain2 Tuning Driver

This KMDF prototype implements a simple IOCTL-based interface to map
perf-state indices to frequencies using hardcoded LUT values taken from
sm8150.dtsi (the Linux device tree). The driver maps the SM8150 freq
domain MMIO regions and writes the perf-state index into offset `0x920`
for Domain2 (third freq-domain). Use this tree only for testing.

Files:
- `Driver.c` - KMDF DriverEntry
- `Device.c` - EvtDeviceAdd, IOCTL queue, MMIO mapping
- `SocFrequencyManagement.c` - LUT mappings and IOCTL handling
- `SocFrequencyManagement.h` - IOCTL / context definitions
- `SocFrequencyManagement.inf` - sample INF for ARM64 installation