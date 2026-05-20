// Copyright 2026 Black-Seraph
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Driver.h"
#include "Driver.tmh"

/// <summary>
/// Initializes the driver. This is the first routine called by the system after
/// the driver is loaded. Specifies the other entry points in the function driver,
/// such as EvtDevice and DriverUnload.
/// </summary>
/// <param name="DriverObject">Represents the instance of the function driver loaded into memory.</param>
/// <param name="RegistryPath">Represents the driver-specific path in the Registry.</param>
/// <returns>STATUS_SUCCESS if successful, STATUS_UNSUCCESSFUL otherwise.</returns>
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG config;
	NTSTATUS status;
	WDF_OBJECT_ATTRIBUTES attributes;

	// Initialize WPP Tracing
#if WPP_ENABLED
	{
	#if UMDF_VERSION_MAJOR == 2 && UMDF_VERSION_MINOR == 0
		WPP_INIT_TRACING(MYDRIVER_TRACING_ID);
	#else
		WPP_INIT_TRACING(DriverObject, RegistryPath);
	#endif
	}
#endif
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	// Register a cleanup callback so that we can call WPP_CLEANUP when the framework driver object is deleted during driver unload.
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.EvtCleanupCallback = DuoControllerEvtDriverContextCleanup;

	WDF_DRIVER_CONFIG_INIT(&config,
		DuoControllerEvtDeviceAdd
		);

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
		);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
			"WdfDriverCreate failed %!STATUS!",
			status);
#if WPP_ENABLED
		{
#if UMDF_VERSION_MAJOR == 2 && UMDF_VERSION_MINOR == 0
			WPP_CLEANUP();
#else
			WPP_CLEANUP(DriverObject);
#endif
		}
#endif
		return status;
	}
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

	return status;
}

/// <summary>
/// Called by the framework in response to the AddDevice call from the PnP manager.
/// Creates and initializes a device object to represent a new instance of the device.
/// </summary>
/// <param name="Driver">Handle to a framework driver object created in DriverEntry.</param>
/// <param name="DeviceInit">Pointer to a framework-allocated WDFDEVICE_INIT structure.</param>
/// <returns>NTSTATUS</returns>
NTSTATUS DuoControllerEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
	NTSTATUS status;

	UNREFERENCED_PARAMETER(Driver);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = DuoControllerCreateDevice(DeviceInit);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

/// <summary>
/// Frees all the resources allocated in DriverEntry.
/// </summary>
/// <param name="DriverObject">Handle to a WDF Driver object.</param>
VOID DuoControllerEvtDriverContextCleanup(_In_ WDFOBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	// Stop WPP Tracing
#if defined(WPP_ENABLED)
	{
#if UMDF_VERSION_MAJOR == 2 && UMDF_VERSION_MINOR == 0
		WPP_CLEANUP();
#else
		WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
#endif
	}
#endif
}