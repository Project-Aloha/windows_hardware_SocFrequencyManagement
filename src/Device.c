/* Device creation and IOCTL handling for SocFrequencyManagement prototype */

#include <ntddk.h>
#include <wdf.h>
#include "../include/Device.h"
#include "../include/SocFrequencyManagement.h"

// Timer callback prototype
EVT_WDF_TIMER QcomPeriodicTimerFunc;

// Cleanup callback to unmap MMIO regions
VOID
DeviceEvtCleanup(_In_ WDFOBJECT DeviceObject)
{
    WDFDEVICE device = (WDFDEVICE)DeviceObject;
    PDEVICE_CONTEXT devCtx = DeviceGetContext(device);
    const SIZE_T mapSize = 0x1400;
    int i;

    // Stop and delete timer if exists
    if (devCtx->PeriodicTimer) {
        WdfTimerStop(devCtx->PeriodicTimer, TRUE);
        devCtx->PeriodicTimer = NULL;
    }

    for (i = 0; i < 3; i++) {
        if (devCtx->MmioBase[i]) {
            MmUnmapIoSpace(devCtx->MmioBase[i], mapSize);
            devCtx->MmioBase[i] = NULL;
            KdPrint(("SocFrequencyManagement: unmapped domain %d\n", i));
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
        KdPrint(("SocFrequencyManagement: WdfDeviceCreate failed 0x%08x\n", status));
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
                    KdPrint(("SocFrequencyManagement: MmMapIoSpace failed for domain %d phys=0x%llx\n", i, physAddrs[i].QuadPart));
                } else {
                    devCtx->MmioBase[i] = base;
                    KdPrint(("SocFrequencyManagement: mapped domain %d -> %p\n", i, base));
                }
            }
        }

        // Probe each mapped domain: check reg_enable and reg_dcvs_ctrl
        {
            const ULONG reg_enable_off = 0x0;
            const ULONG reg_dcvs_ctrl_off = 0xbc;
            int d;

            for (d = 0; d < 3; d++) {
                PVOID b = devCtx->MmioBase[d];
                if (b == NULL) {
                    devCtx->PerCoreDcvs[d] = FALSE;
                    devCtx->LutValid[d] = FALSE;
                    KdPrint(("SocFrequencyManagement: domain %d not mapped, skipping\n", d));
                    continue;
                }

                // Read reg_enable; if not enabled, unmap and skip domain
                {
                    ULONG en = READ_REGISTER_ULONG((ULONG *)((PUCHAR)b + reg_enable_off));
                    if ((en & 0x1) == 0) {
                        KdPrint(("SocFrequencyManagement: domain %d not enabled (reg_enable=0x%08x), unmapping\n", d, en));
                        MmUnmapIoSpace(devCtx->MmioBase[d], mapSize);
                        devCtx->MmioBase[d] = NULL;
                        devCtx->PerCoreDcvs[d] = FALSE;
                        devCtx->LutValid[d] = FALSE;
                        continue;
                    }
                }

                // Read reg_dcvs_ctrl bit0: if set, enable per-core DCVS behaviour
                {
                    ULONG dcvs = READ_REGISTER_ULONG((ULONG *)((PUCHAR)b + reg_dcvs_ctrl_off));
                    devCtx->PerCoreDcvs[d] = ((dcvs & 0x1) != 0) ? TRUE : FALSE;
                    KdPrint(("SocFrequencyManagement: domain %d reg_dcvs_ctrl=0x%08x per_core=%d\n", d, dcvs, devCtx->PerCoreDcvs[d]));
                }

                // Parse LUT from hardware registers
                {
                    NTSTATUS lutStatus = QcomParseLut(device, (UINT32)d);
                    if (!NT_SUCCESS(lutStatus)) {
                        KdPrint(("SocFrequencyManagement: domain %d LUT parsing failed 0x%08x, will use hardcoded fallback\n", d, lutStatus));
                    }
                }
            }

            // On driver load, adjust Domain2 based on Domain1 frequency
            {
                NTSTATUS s = QcomAdjustDomain2BasedOn1(device);
                if (!NT_SUCCESS(s))
                    KdPrint(("SocFrequencyManagement: initial Domain2 adjustment failed 0x%08x\n", s));
            }
        }
    }

    // Create a periodic WDF timer to adjust Domain2 periodically
    {
        WDFTIMER timer;
        WDF_TIMER_CONFIG timerConfig;
        WDF_OBJECT_ATTRIBUTES timerAttr;
        const ULONG periodMs = 1000; // 1 seconds

        WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, QcomPeriodicTimerFunc, periodMs);
        WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
        timerAttr.ParentObject = device;

        status = WdfTimerCreate(&timerConfig, &timerAttr, &timer);
        if (!NT_SUCCESS(status)) {
            KdPrint(("SocFrequencyManagement: WdfTimerCreate failed 0x%08x\n", status));
        } else {
            devCtx->PeriodicTimer = timer;
            WdfTimerStart(timer, WDF_REL_TIMEOUT_IN_MS(periodMs));
            KdPrint(("SocFrequencyManagement: periodic timer started (%u ms)\n", periodMs));
        }
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = QcomEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("SocFrequencyManagement: WdfIoQueueCreate failed 0x%08x\n", status));
        return status;
    }

    KdPrint(("SocFrequencyManagement: device created\n"));

    return STATUS_SUCCESS;
}
