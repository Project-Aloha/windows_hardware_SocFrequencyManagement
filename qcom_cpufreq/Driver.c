/*
 * qcom_cpufreq Windows KMDF prototype
 * Minimal DriverEntry that registers EvtDeviceAdd
 */

#include <ntddk.h>
#include <wdf.h>
#include "Device.h"

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, QcomEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES,
                             &config,
                             WDF_NO_HANDLE);

    if (!NT_SUCCESS(status)) {
        KdPrint(("qcom_cpufreq: WdfDriverCreate failed 0x%08x\n", status));
    } else {
        KdPrint(("qcom_cpufreq: DriverEntry success\n"));
    }

    return status;
}
