/* Device creation and IOCTL handling for qcom_cpufreq prototype */

#include <ntddk.h>
#include <wdf.h>
#include "Device.h"
#include "qcom_cpufreq.h"

// Cleanup callback to unmap MMIO regions
VOID
DeviceEvtCleanup(_In_ WDFOBJECT DeviceObject)
{
    WDFDEVICE device = (WDFDEVICE)DeviceObject;
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);
    const SIZE_T mapSize = 0x1400;
    int i;

    for (i = 0; i < 3; i++) {
        if (devCtx->MmioBase[i]) {
            MmUnmapIoSpace(devCtx->MmioBase[i], mapSize);
            devCtx->MmioBase[i] = NULL;
            KdPrint(("qcom_cpufreq: unmapped domain %d\n", i));
        }
    }
}

NTSTATUS
QcomEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    PDEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = DeviceEvtCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("qcom_cpufreq: WdfDeviceCreate failed 0x%08x\n", status));
        return status;
    }

    devCtx = DeviceGetContext(device);
    RtlZeroMemory(devCtx, sizeof(*devCtx));

    // Map fixed MMIO regions from sm8150.dtsi (hardcoded physical addresses)
    {
        const PHYSICAL_ADDRESS physAddrs[3] = {
            { .QuadPart = 0x0000000018323000ULL },
            { .QuadPart = 0x0000000018325800ULL },
            { .QuadPart = 0x0000000018327800ULL },
        };
        const SIZE_T mapSize = 0x1400;
        int i;

        for (i = 0; i < 3; i++) {
            if (devCtx->MmioBase[i] == NULL) {
                PVOID base = MmMapIoSpace(physAddrs[i], mapSize, MmNonCached);
                if (base == NULL) {
                    KdPrint(("qcom_cpufreq: MmMapIoSpace failed for domain %d phys=0x%llx\n", i, physAddrs[i].QuadPart));
                } else {
                    devCtx->MmioBase[i] = base;
                    KdPrint(("qcom_cpufreq: mapped domain %d -> %p\n", i, base));
                }
            }
        }

        // On driver load, set Domain2 to nearest LUT entry to 900MHz.
        // sm8150 big-cluster entries: 825600, 940800, ... -> choose index 1 (940800 kHz)
        {
            NTSTATUS s;
            UINT32 idx = 1;
            devCtx->CurrentIndexDomain2 = idx;
            s = QcomSetPerfState(device, 2, idx);
            if (!NT_SUCCESS(s))
                KdPrint(("qcom_cpufreq: failed to set Domain2 perf state idx=%u status=0x%08x\n", idx, s));
            else
                KdPrint(("qcom_cpufreq: Domain2 perf state set to idx=%u (approx 940800 kHz)\n", idx));
        }
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = QcomEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("qcom_cpufreq: WdfIoQueueCreate failed 0x%08x\n", status));
        return status;
    }

    KdPrint(("qcom_cpufreq: device created\n"));

    return STATUS_SUCCESS;
}
