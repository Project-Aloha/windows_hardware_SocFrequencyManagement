#pragma once

#include <ntddk.h>
#include <wdf.h>

NTSTATUS QcomEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );
