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

#include "DuoController.h"
#include <winternl.h>
#include <winstring.h>
#include <initguid.h>
#include <devpkey.h>
#include <devquery.h>
#include <devquerydef.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <stdio.h>
#include "Public.h"

#pragma comment(lib, "mincore.lib")
#pragma comment(lib, "onecore.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "setupapi.lib")

#ifndef STATUS_WAIT_0
#define STATUS_WAIT_0 ((DWORD)0x00000000L)
#endif

/// <summary>
/// A dummy value used to target no process, essentially acting as a input silencer.
/// </summary>
#define NO_PROCESS 0x100000000000000

/// <summary>
/// The supported synthetic controller input report types.
/// </summary>
typedef enum _SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE
{
	/// <summary>
	/// The controller input report which contains all of its regular buttons, axes and triggers.
	/// </summary>
	SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE_CONTROLLER = 0,

	/// <summary>
	/// The virtual key input report which contains the Guide button state.
	/// </summary>
	SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE_VKEY = 1
} SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE;

/// <summary>
/// The supported synthetic controller output report types.
/// </summary>
typedef enum _SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE
{
	/// <summary>
	/// The controller output report which contains the rumble motor speeds.
	/// </summary>
	SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE_CONTROLLER = 0
} SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE;

#pragma pack(push, 1)

/// <summary>
/// The virtual key input report structure used to report the Guide button state.
/// </summary>
typedef struct _SYNTHETIC_CONTROLLER_VKEY_INPUT_REPORT
{
	INT16 VirtualKey; // Must be set to 0, internally hardcoded to VK_LWIN
	UINT8 State; // 0 = Released, 1 = Pressed
} SYNTHETIC_CONTROLLER_VKEY_INPUT_REPORT;

/// <summary>
/// The DuoController structure.
/// </summary>
typedef struct _DUO_CONTROLLER
{
	DUO_CONTROLLER_TYPE Type;

	DuoController_VibrationReportCallback_t VibrationReportCallback;
	void* VibrationReportCallbackContext;

	// Xbox-specific fields
	void* SyntheticHandle;
	HDEVQUERY DeviceQuery;
	DUO_CONTROLLER_INPUT_REPORT LastXboxInputReport;

	// DS4-specific fields
	WCHAR Ds4InstanceId[256];
	HANDLE Ds4InputMapping;
	HANDLE Ds4OutputMapping;
	LPVOID Ds4InputView;
	LPVOID Ds4OutputView;
	HANDLE Ds4InputEvent;
	HANDLE Ds4OutputEvent;
	HANDLE Ds4FfbThread;
	HANDLE Ds4FfbStopEvent;
	CRITICAL_SECTION Ds4Cs;
	DUO_CONTROLLER_INPUT_REPORT_DS4 LastDs4InputReport;
} DUO_CONTROLLER;

/// <summary>
/// Defines a WNF type ID.
/// </summary>
typedef struct _WNF_TYPE_ID {
	GUID TypeId;
} WNF_TYPE_ID, * PWNF_TYPE_ID;

/// <summary>
/// Defines a WNF state name.
/// </summary>
typedef struct _WNF_STATE_NAME {
	ULONG Data[2];
} WNF_STATE_NAME, * PWNF_STATE_NAME;

#pragma pack(pop)

/// <summary>
/// The WNF state name used by ISM.dll (dwm.exe) to report focus changes.
/// </summary>
static WNF_STATE_NAME WNF_SHEL_FOCUS_CHANGE = { 0xA3BC7875, 0xD83063E };

/// <summary>
/// Receives output reports from the synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="outputReportType">The output report type</param>
/// <param name="outputReportBuffer">The output report buffer</param>
/// <param name="outputReportBufferSize">The size of the output report buffer</param>
/// <param name="context">The context</param>
typedef void(WINAPI *SyntheticController_ReportCallback_t)(void* controller, unsigned int outputReportType, void* outputReportBuffer, unsigned int outputReportBufferSize, void* context);

/// <summary>
/// Creates a new synthetic controller.
/// </summary>
/// <param name="controllerType">Controller type</param>
/// <param name="controller">Receives the created controller</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_CreateController_t)(unsigned long controllerType, void** controller);

/// <summary>
/// Sets the target process for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="processId">The target process ID, or NO_PROCESS to silence all controller output</param>
/// <param name="inputReportType">The input report type</param>
/// <param name="inputReportBuffer">The input report buffer</param>
/// <param name="inputReportBufferSize">The size of the input report buffer</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_SetTargetProcess_t)(void* controller, unsigned long long processId /* or NO_PROCESS */, SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE inputReportType, void* inputReportBuffer, unsigned int inputReportBufferSize);

/// <summary>
/// Registers an output report callback for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="outputReportType">The output report type to register for</param>
/// <param name="callback">The report callback</param>
/// <param name="context">The context to pass to the callback</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_RegisterReportCallback_t)(void* controller, SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE outputReportType, SyntheticController_ReportCallback_t callback, void* context);

/// <summary>
/// Connects the given synthetic controller to the system.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_Connect_t)(void* controller);

/// <summary>
/// Sends an input report to the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="inputReportType">The input report type</param>
/// <param name="inputReportBuffer">The input report buffer</param>
/// <param name="inputReportBufferSize">The size of the input report buffer</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_SendReport_t)(void* controller, SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE inputReportType, void* inputReportBuffer, unsigned int inputReportBufferSize);

/// <summary>
/// Disconnects the given synthetic controller from the system.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_Disconnect_t)(void* controller);

/// <summary>
/// Unregisters an output report callback for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="outputReportType">The output report type to unregister for</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_UnregisterReportCallback_t)(void* controller, SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE outputReportType);

/// <summary>
/// Removes the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_RemoveController_t)(void* controller);

/// <summary>
/// Gets the device ID of the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="deviceId">Receives the device ID</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI *SyntheticController_GetDeviceId_t)(void* controller, unsigned long long* deviceId);

/// <summary>
/// Sets properties on a device object.
/// </summary>
/// <param name="ObjectType">The object type</param>
/// <param name="pszObjectId">The object ID</param>
/// <param name="pcPropertyCount">The property count</param>
/// <param name="ppProperties">The properties</param>
/// <returns>Result</returns>
typedef HRESULT(WINAPI* DevSetObjectProperties_t)(DEV_OBJECT_TYPE ObjectType, PCWSTR pszObjectId, ULONG pcPropertyCount, const DEVPROPERTY* ppProperties);

/// <summary>
/// Publishes WNF state data.
/// </summary>
/// <param name="StateName">The WNF state name</param>
/// <param name="TypeId">The WNF type ID, can be NULL</param>
/// <param name="Buffer">The buffer containing the state data, can be NULL</param>
/// <param name="Length">The length of the buffer</param>
/// <param name="ExplicitScope">The explicit scope, can be NULL</param>
/// <returns>Result</returns>
typedef NTSTATUS(WINAPI* RtlPublishWnfStateData_t)(WNF_STATE_NAME StateName, const PWNF_TYPE_ID TypeId, const VOID* Buffer, ULONG Length, const VOID* ExplicitScope);

/// <summary>
/// Creates a new synthetic controller.
/// </summary>
/// <param name="controllerType">Controller type</param>
/// <param name="controller">Receives the created controller</param>
/// <returns>Result</returns>
SyntheticController_CreateController_t SyntheticController_CreateController;

/// <summary>
/// Sets the target process for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="processId">The target process ID, or NO_PROCESS to silence all controller output</param>
/// <param name="inputReportType">The input report type</param>
/// <param name="inputReportBuffer">The input report buffer</param>
/// <param name="inputReportBufferSize">The size of the input report buffer</param>
/// <returns>Result</returns>
SyntheticController_SetTargetProcess_t SyntheticController_SetTargetProcess;

/// <summary>
/// Registers an output report callback for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="outputReportType">The output report type to register for</param>
/// <param name="callback">The report callback</param>
/// <param name="context">The context to pass to the callback</param>
/// <returns>Result</returns>
SyntheticController_RegisterReportCallback_t SyntheticController_RegisterReportCallback;

/// <summary>
/// Connects the given synthetic controller to the system.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
SyntheticController_Connect_t SyntheticController_Connect;

/// <summary>
/// Sends an input report to the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="inputReportType">The input report type</param>
/// <param name="inputReportBuffer">The input report buffer</param>
/// <param name="inputReportBufferSize">The size of the input report buffer</param>
/// <returns>Result</returns>
SyntheticController_SendReport_t SyntheticController_SendReport;

/// <summary>
/// Disconnects the given synthetic controller from the system.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
SyntheticController_Disconnect_t SyntheticController_Disconnect;

/// <summary>
/// Unregisters an output report callback for the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="outputReportType">The output report type to unregister for</param>
/// <returns>Result</returns>
SyntheticController_UnregisterReportCallback_t SyntheticController_UnregisterReportCallback;

/// <summary>
/// Removes the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <returns>Result</returns>
SyntheticController_RemoveController_t SyntheticController_RemoveController;

/// <summary>
/// Gets the device ID of the given synthetic controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="deviceId">Receives the device ID</param>
/// <returns>Result</returns>
SyntheticController_GetDeviceId_t SyntheticController_GetDeviceId;

/// <summary>
/// Sets properties on a device object.
/// </summary>
/// <param name="ObjectType">The object type</param>
/// <param name="pszObjectId">The object ID</param>
/// <param name="pcPropertyCount">The property count</param>
/// <param name="ppProperties">The properties</param>
/// <returns>Result</returns>
DevSetObjectProperties_t DevSetObjectProperties;

/// <summary>
/// Publishes WNF state data.
/// </summary>
/// <param name="StateName">The WNF state name</param>
/// <param name="TypeId">The WNF type ID, can be NULL</param>
/// <param name="Buffer">The buffer containing the state data, can be NULL</param>
/// <param name="Length">The length of the buffer</param>
/// <param name="ExplicitScope">The explicit scope, can be NULL</param>
/// <returns>Result</returns>
RtlPublishWnfStateData_t RtlPublishWnfStateData;

/// <summary>
/// The current session ID.
/// </summary>
static DWORD SessionId;

/// <summary>
/// The current state of the DuoController library.
/// </summary>
static BOOL Initialized;

/// <summary>
/// The current session's foreground window process ID.
/// </summary>
static DWORD ForegroundWindowProcessId;

/// <summary>
/// The created controllers.
/// </summary>
static DUO_CONTROLLER** Controllers = NULL;

/// <summary>
/// The number of created controllers.
/// </summary>
static DWORD ControllerCount = 0;

/// <summary>
/// Initializes the Windows Runtime for the current thread if not already initialized.
/// </summary>
static void InitializeWindowsRuntimeForCurrentThread()
{
	// Create a HSTRING for Windows.Internal.Gaming.SWDDeviceStatics
	const wchar_t* type_name = L"Windows.Internal.Gaming.SWDDeviceStatics";
	HSTRING_HEADER header;
	memset(&header, 0, sizeof(header));
	HSTRING string = NULL;
	if (SUCCEEDED(WindowsCreateStringReference(type_name, (UINT32)wcslen(type_name), &header, &string)))
	{
		// The IID for Windows.Internal.Gaming.SWDDeviceStatics
		const GUID IID_SWDeviceStatics = {
			0x5189313c, 0xfc43, 0x41b2, { 0x82, 0xcc, 0x27, 0x1a, 0x0e, 0xe2, 0x9e, 0x80 }
		};

		// Get the activation factory for Windows.Internal.Gaming.SWDDeviceStatics
		void* factory = NULL;
		if (FAILED(RoGetActivationFactory(string, &IID_SWDeviceStatics, &factory)))
		{
			// Initialize the Windows Runtime for the current thread
			RoInitialize(RO_INIT_MULTITHREADED);
		}
	}
}

/// <summary>
/// The callback that is called when a device is added, removed or updated.
/// </summary>
/// <param name="query">The query that triggered this callback</param>
/// <param name="context">The context</param>
/// <param name="actionData">The action data</param>
static void WINAPI DuoController_DeviceChanged(_In_ HDEVQUERY query, _In_opt_ PVOID context, _In_ const DEV_QUERY_RESULT_ACTION_DATA* actionData)
{
	// Unreferenced parameters
	(void)query;
	(void)context;

	// A new device has been added
	if (actionData->Action == DevQueryResultAdd)
	{
		// Set the session ID property
		DEVPROPERTY deviceProperty;
		deviceProperty.CompKey.Key = DEVPKEY_Device_SessionId;
		deviceProperty.CompKey.Store = DEVPROP_STORE_SYSTEM;
		deviceProperty.CompKey.LocaleName = NULL;
		deviceProperty.Type = DEVPROP_TYPE_UINT32;
		deviceProperty.BufferSize = sizeof(SessionId);
		deviceProperty.Buffer = (PBYTE)&SessionId;
		DevSetObjectProperties(DevObjectTypeDevice, actionData->Data.DeviceObject.pszObjectId, 1, &deviceProperty);
	}
}

/// <summary>
/// Gets the process ID of the current session's foreground window.
/// </summary>
/// <param name="timeoutMs">The timeout in milliseconds</param>
/// <returns>The process ID of the foreground window, or 0 if the timeout is reached</returns>
DWORD GetForegroundWindowProcessId(DWORD timeoutMs)
{
	// The foreground window process ID
	DWORD processId = 0;

	// Get the start time
	DWORD startTime = GetTickCount();

	do
	{
		// Try getting the current session's foreground window
		HWND hwnd = GetForegroundWindow();

		// We've found the current session's foreground window
		if (hwnd != NULL)
		{
			// Get the process ID of the foreground window
			GetWindowThreadProcessId(hwnd, &processId);

			// Return the process ID
			break;
		}

		// Wait a bit before re-trying
		Sleep(10);
	} while ((GetTickCount64() - startTime) < timeoutMs);

	// Return the foreground window process ID
	return processId;
}

/// <summary>
/// Updates the GameInput driver's focus state.
/// </summary>
/// <returns>Result</returns>
static NTSTATUS UpdateGameInputDriverFocusState()
{
	// The result
	NTSTATUS result = ERROR_PROC_NOT_FOUND;

	// We can restore the focus the GIP driver needs
	if (RtlPublishWnfStateData != NULL)
	{
		// Focus the session's current foreground window process
		result = RtlPublishWnfStateData(WNF_SHEL_FOCUS_CHANGE, NULL, &ForegroundWindowProcessId, sizeof(ForegroundWindowProcessId), NULL);
	}

	// Return the result
	return result;
}

/// <summary>
/// The callback that is called when an output report is received from a synthetic controller.
/// </summary>
/// <param name="controller">Synthetic controller handle</param>
/// <param name="outputReportType">Output report type</param>
/// <param name="outputReportBuffer">Output report buffer</param>
/// <param name="outputReportBufferSize">Output report buffer size</param>
/// <param name="context">Context</param>
static void CALLBACK DuoController_OutputReportReceived(void* controller, unsigned int outputReportType, void* outputReportBuffer, unsigned int outputReportBufferSize, void* context)
{
	// Unreferenced parameters
	(void)controller;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// Update the current session's foreground window process ID
	ForegroundWindowProcessId = GetForegroundWindowProcessId(500);

	// Cast the context to a DuoController
	DUO_CONTROLLER* duoController = (DUO_CONTROLLER*)context;

	// Cast the controller output report buffer
	DUO_CONTROLLER_FORCE_FEEDBACK_REPORT* outputReport = (DUO_CONTROLLER_FORCE_FEEDBACK_REPORT*)outputReportBuffer;

	// We've received a controller output report and can forward it
	if (outputReportType == SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE_CONTROLLER &&
		outputReportBuffer != NULL && outputReportBufferSize >= sizeof(DUO_CONTROLLER_FORCE_FEEDBACK_REPORT) &&
		duoController != NULL && duoController->VibrationReportCallback != NULL)
	{
		// Forward the vibration data to the registered callback
		duoController->VibrationReportCallback(duoController, outputReport, duoController->VibrationReportCallbackContext);
	}
}

/// <summary>
/// Converts a Win32 error code to an HRESULT.
/// </summary>
/// <param name="error">The Win32 error code</param>
/// <returns>The HRESULT</returns>
static HRESULT Ds4Win32ErrorToHresult(DWORD error)
{
	switch (error)
	{
	case ERROR_SUCCESS:
		return S_OK;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	case ERROR_ACCESS_DENIED:
		return E_ACCESSDENIED;
	case ERROR_INVALID_PARAMETER:
		return E_INVALIDARG;
	case ERROR_PIPE_NOT_CONNECTED:
	case ERROR_NO_DATA:
	case ERROR_BROKEN_PIPE:
		return HRESULT_FROM_WIN32(ERROR_PIPE_NOT_CONNECTED);
	case ERROR_OUTOFMEMORY:
		return E_OUTOFMEMORY;
	default:
		return HRESULT_FROM_WIN32(error);
	}
}

/// <summary>
/// Sanitizes a device instance ID for use as a pipe name component.
/// </summary>
/// <param name="instanceId">The device instance ID</param>
/// <param name="sanitized">Receives the sanitized string</param>
/// <param name="sanitizedSize">The size of the sanitized buffer in characters</param>
static void SanitizeInstanceIdForPipeName(const WCHAR* instanceId, WCHAR* sanitized, size_t sanitizedSize)
{
	size_t i;
	for (i = 0; i < sanitizedSize - 1 && instanceId[i] != L'\0'; i++)
	{
		sanitized[i] = (instanceId[i] == L'\\') ? L'_' : instanceId[i];
	}
	sanitized[i] = L'\0';
}

/// <summary>
/// Checks if the current process is running with elevated privileges.
/// </summary>
/// <returns>TRUE if elevated, FALSE otherwise</returns>
static BOOL IsProcessElevated()
{
	HANDLE token = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return FALSE;

	TOKEN_ELEVATION elevation;
	DWORD size = 0;
	BOOL elevated = FALSE;
	if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
		elevated = elevation.TokenIsElevated;

	CloseHandle(token);
	return elevated;
}

/// <summary>
/// Installs a new DuoController device and returns its instance ID.
/// </summary>
/// <param name="instanceId">Receives the instance ID of the newly created device</param>
/// <param name="instanceIdSize">The size of the instance ID buffer in characters</param>
/// <returns>Result</returns>
static HRESULT InstallDuoControllerDevice(WCHAR* instanceId, DWORD instanceIdSize)
{
	// The result
	HRESULT result = S_OK;

	// Get the DuoController DLL path to locate the DuoController driver files
	HMODULE hMod = NULL;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&DuoController_Initialize, &hMod))
		return HRESULT_FROM_WIN32(GetLastError());

	WCHAR dllPath[MAX_PATH];
	if (GetModuleFileNameW(hMod, dllPath, MAX_PATH) == 0)
		return HRESULT_FROM_WIN32(GetLastError());

	// Find the last backslash to get the DuoController directory
	WCHAR* lastSlash = wcsrchr(dllPath, L'\\');
	if (lastSlash == NULL)
		return E_UNEXPECTED;

	*(lastSlash + 1) = L'\0';

	// Build the INF path: DuoController directory\DuoController.inf
	WCHAR infPath[MAX_PATH];
	wcscpy_s(infPath, MAX_PATH, dllPath);
	wcscat_s(infPath, MAX_PATH, L"DuoController.inf");

	// Check if the driver files exist
	if (GetFileAttributesW(infPath) == INVALID_FILE_ATTRIBUTES)
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

	// We need elevation to install a device
	if (!IsProcessElevated())
		return E_ACCESSDENIED;

	// The hardware ID for the DS4 device (matches DuoController.inf)
	const WCHAR* hardwareId = L"Root\\VID_054C&PID_05C4";

	// The device ID seed
	const WCHAR* deviceIdSeed = L"VID_054C&PID_05C4&DUOCONTROLLER";

	// Get the full INF path
	WCHAR fullInfPath[MAX_PATH];
	if (!GetFullPathNameW(infPath, MAX_PATH, fullInfPath, NULL))
		return HRESULT_FROM_WIN32(GetLastError());

	// Get the class GUID from the INF
	GUID classGuid;
	WCHAR className[MAX_CLASS_NAME_LEN];
	DWORD requiredSize = 0;
	if (!SetupDiGetINFClassW(fullInfPath, &classGuid, className, MAX_CLASS_NAME_LEN, &requiredSize))
		return HRESULT_FROM_WIN32(GetLastError());

	// Create a device info list for the class
	HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(&classGuid, NULL);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());

	// Create the device info element
	SP_DEVINFO_DATA devInfoData;
	ZeroMemory(&devInfoData, sizeof(devInfoData));
	devInfoData.cbSize = sizeof(devInfoData);

	if (!SetupDiCreateDeviceInfoW(hDevInfo, deviceIdSeed, &classGuid, NULL, NULL, DICD_GENERATE_ID, &devInfoData))
	{
		result = HRESULT_FROM_WIN32(GetLastError());
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return result;
	}

	// Set the hardware ID property
	DWORD hwIdLen = (DWORD)(wcslen(hardwareId) * sizeof(WCHAR));
	if (!SetupDiSetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_HARDWAREID, (const BYTE*)hardwareId, hwIdLen + sizeof(WCHAR)))
	{
		result = HRESULT_FROM_WIN32(GetLastError());
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return result;
	}

	// Register the device with the system
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hDevInfo, &devInfoData))
	{
		result = HRESULT_FROM_WIN32(GetLastError());
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return result;
	}

	// Get the instance ID assigned to our new device
	if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId, instanceIdSize, NULL))
	{
		result = HRESULT_FROM_WIN32(GetLastError());
		SetupDiDestroyDeviceInfoList(hDevInfo);
		return result;
	}

	// Open or create the Duo registry key
	HKEY duoRegistryKey;
	LONG status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Duo", 0, NULL, REG_OPTION_VOLATILE, KEY_READ | KEY_WRITE, NULL, &duoRegistryKey, NULL);
	if (status == ERROR_SUCCESS)
	{
		// The value name
		LPCWSTR valueName = L"DuoControllerSessionId";

		// Write the session ID to the registry so that the driver can read it when it starts
		RegSetValueExW(duoRegistryKey, valueName, 0, REG_DWORD, (const BYTE*)&SessionId, sizeof(SessionId));

		// Install/update the driver for the device
		BOOL rebootRequired = FALSE;
		if (!UpdateDriverForPlugAndPlayDevicesW(NULL, hardwareId, fullInfPath, 0, &rebootRequired))
		{
			result = HRESULT_FROM_WIN32(GetLastError());
			SetupDiDestroyDeviceInfoList(hDevInfo);
			return result;
		}

		// Clean up the registry
		RegDeleteValueW(duoRegistryKey, valueName);

		// Close the registry key
		RegCloseKey(duoRegistryKey);
	}

	// Clean up
	SetupDiDestroyDeviceInfoList(hDevInfo);

	return result;
}

/// <summary>
/// Removes an DuoController device by its instance ID.
/// </summary>
/// <param name="instanceId">The instance ID of the device to remove</param>
/// <returns>Result</returns>
static HRESULT RemoveDuoControllerDevice(const WCHAR* instanceId)
{
	// We need elevation to remove a device
	if (!IsProcessElevated())
		return E_ACCESSDENIED;

	// Enumerate all devices
	HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());

	HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	SP_DEVINFO_DATA devInfoData;
	devInfoData.cbSize = sizeof(devInfoData);
	DWORD index = 0;

	while (SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData))
	{
		WCHAR devInstanceId[256];
		if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, devInstanceId, ARRAYSIZE(devInstanceId), NULL))
		{
			index++;
			continue;
		}

		// We've found the device we need to remove
		if (_wcsicmp(devInstanceId, instanceId) == 0)
		{
			// Prepare the remove parameters
			SP_REMOVEDEVICE_PARAMS removeParams;
			ZeroMemory(&removeParams, sizeof(removeParams));
			removeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
			removeParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
			removeParams.Scope = DI_REMOVEDEVICE_GLOBAL;

			// Set the class install parameters
			if (!SetupDiSetClassInstallParamsW(hDevInfo, &devInfoData,
				(PSP_CLASSINSTALL_HEADER)&removeParams, sizeof(removeParams)))
			{
				result = HRESULT_FROM_WIN32(GetLastError());
				break;
			}

			// Remove the device (this should cascade to HID child devices)
			if (!SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devInfoData))
			{
				// DIF_REMOVE failed, try SetupDiRemoveDevice directly as a fallback.
				// This ensures the device node and its HID children are removed
				// even if the class installer rejects the DIF_REMOVE request.
				if (!SetupDiRemoveDevice(hDevInfo, &devInfoData))
				{
					result = HRESULT_FROM_WIN32(GetLastError());
					break;
				}
			}

			result = S_OK;
			break;
		}

		index++;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
	return result;
}

/// <summary>
/// Opens the DS4 input shared memory region and event for the given device instance ID.
/// </summary>
/// <param name="controller">The DuoController to connect</param>
/// <returns>Result</returns>
static HRESULT Ds4ConnectInput(DUO_CONTROLLER* controller)
{
	// Build the shared memory names
	WCHAR sanitized[256];
	SanitizeInstanceIdForPipeName(controller->Ds4InstanceId, sanitized, ARRAYSIZE(sanitized));

	WCHAR mappingName[512];
	swprintf_s(mappingName, ARRAYSIZE(mappingName), L"Global\\Duo_%s_input", sanitized);

	WCHAR eventName[512];
	swprintf_s(eventName, ARRAYSIZE(eventName), L"Global\\Duo_%s_input_event", sanitized);

	// Retry loop: wait for DuoController to create the shared memory
	DWORD startTime = GetTickCount();
	HANDLE hMapping = NULL;

	while (GetTickCount() - startTime < 1000)
	{
		hMapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, mappingName);
		if (hMapping != NULL)
			break;
		Sleep(10);
	}

	if (hMapping == NULL)
	{
		DWORD err = GetLastError();
		return (err == ERROR_FILE_NOT_FOUND) ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : Ds4Win32ErrorToHresult(err);
	}

	// Map the input shared memory
	LPVOID view = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, 0);
	if (view == NULL)
	{
		CloseHandle(hMapping);
		return Ds4Win32ErrorToHresult(GetLastError());
	}

	// Open the input event (need EVENT_MODIFY_STATE to SetEvent)
	HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
	if (hEvent == NULL)
	{
		UnmapViewOfFile(view);
		CloseHandle(hMapping);
		return Ds4Win32ErrorToHresult(GetLastError());
	}

	controller->Ds4InputMapping = hMapping;
	controller->Ds4InputView = view;
	controller->Ds4InputEvent = hEvent;
	return S_OK;
}

/// <summary>
/// The thread procedure that reads force feedback data from the DS4 output shared memory.
/// </summary>
/// <param name="param">The DUO_CONTROLLER pointer</param>
/// <returns>Thread exit code</returns>
static DWORD WINAPI Ds4FfbThreadProc(LPVOID param)
{
	DUO_CONTROLLER* controller = (DUO_CONTROLLER*)param;

	HANDLE waitHandles[2] = { controller->Ds4FfbStopEvent, controller->Ds4OutputEvent };

	while (1)
	{
		DWORD wr = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
		if (wr == WAIT_OBJECT_0)
		{
			// Stop event was signaled
			break;
		}

		if (wr == WAIT_OBJECT_0 + 1)
		{
			// Output event was signaled — new FFB data available

			// Read the output report from shared memory
			BYTE* outputMem = (BYTE*)controller->Ds4OutputView;

			// Decode the DS4 output report into a force feedback report
			// DS4 output report: Byte 0 = ReportId (0x05), Byte 1 = flags,
			// Byte 4 = Left motor, Byte 5 = Right motor
			DUO_CONTROLLER_FORCE_FEEDBACK_REPORT ffReport;
			ZeroMemory(&ffReport, sizeof(ffReport));

			ffReport.Flags = SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_RIGHT_MOTOR_VALID |
				SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_LEFT_MOTOR_VALID;
			ffReport.LeftMotor = outputMem[4];
			ffReport.RightMotor = outputMem[5];

#pragma warning(suppress:4366)
			EnterCriticalSection(&controller->Ds4Cs);
			DuoController_VibrationReportCallback_t callback = controller->VibrationReportCallback;
			void* context = controller->VibrationReportCallbackContext;
#pragma warning(suppress:4366)
			LeaveCriticalSection(&controller->Ds4Cs);

			if (callback != NULL)
			{
				callback(controller, &ffReport, context);
			}
		}
	}

	return 0;
}

/// <summary>
/// Opens the DS4 output (FFB) shared memory region and starts the FFB read thread.
/// </summary>
/// <param name="controller">The DuoController to connect</param>
/// <returns>Result</returns>
static HRESULT Ds4ConnectFfb(DUO_CONTROLLER* controller)
{
	// Build the shared memory names
	WCHAR sanitized[256];
	SanitizeInstanceIdForPipeName(controller->Ds4InstanceId, sanitized, ARRAYSIZE(sanitized));

	WCHAR mappingName[512];
	swprintf_s(mappingName, ARRAYSIZE(mappingName), L"Global\\Duo_%s_output", sanitized);

	WCHAR eventName[512];
	swprintf_s(eventName, ARRAYSIZE(eventName), L"Global\\Duo_%s_output_event", sanitized);

	// Retry loop: wait for DuoController to create the shared memory
	DWORD startTime = GetTickCount();
	HANDLE hMapping = NULL;

	while (GetTickCount() - startTime < 1000)
	{
		hMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName);
		if (hMapping != NULL)
			break;
		Sleep(10);
	}

	if (hMapping == NULL)
	{
		DWORD err = GetLastError();
		return (err == ERROR_FILE_NOT_FOUND) ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : Ds4Win32ErrorToHresult(err);
	}

	// Map the output shared memory
	LPVOID view = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
	if (view == NULL)
	{
		CloseHandle(hMapping);
		return Ds4Win32ErrorToHresult(GetLastError());
	}

	// Open the output event (need SYNCHRONIZE to WaitForSingleObject)
	HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, eventName);
	if (hEvent == NULL)
	{
		UnmapViewOfFile(view);
		CloseHandle(hMapping);
		return Ds4Win32ErrorToHresult(GetLastError());
	}

	controller->Ds4OutputMapping = hMapping;
	controller->Ds4OutputView = view;
	controller->Ds4OutputEvent = hEvent;

	// Create the stop event
	controller->Ds4FfbStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!controller->Ds4FfbStopEvent)
	{
		CloseHandle(hEvent);
		UnmapViewOfFile(view);
		CloseHandle(hMapping);
		controller->Ds4OutputEvent = NULL;
		controller->Ds4OutputView = NULL;
		controller->Ds4OutputMapping = NULL;
		return HRESULT_FROM_WIN32(GetLastError());
	}

	// Create the FFB read thread
	controller->Ds4FfbThread = CreateThread(NULL, 0, Ds4FfbThreadProc, controller, 0, NULL);
	if (!controller->Ds4FfbThread)
	{
		CloseHandle(controller->Ds4FfbStopEvent);
		controller->Ds4FfbStopEvent = NULL;
		CloseHandle(hEvent);
		controller->Ds4OutputEvent = NULL;
		UnmapViewOfFile(view);
		controller->Ds4OutputView = NULL;
		CloseHandle(hMapping);
		controller->Ds4OutputMapping = NULL;
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return S_OK;
}

/// <summary>
/// Disconnects the DS4 input shared memory.
/// </summary>
/// <param name="controller">The DuoController</param>
static void Ds4DisconnectInput(DUO_CONTROLLER* controller)
{
	if (controller->Ds4InputEvent != NULL)
	{
		CloseHandle(controller->Ds4InputEvent);
		controller->Ds4InputEvent = NULL;
	}
	if (controller->Ds4InputView != NULL)
	{
		UnmapViewOfFile(controller->Ds4InputView);
		controller->Ds4InputView = NULL;
	}
	if (controller->Ds4InputMapping != NULL)
	{
		CloseHandle(controller->Ds4InputMapping);
		controller->Ds4InputMapping = NULL;
	}
}

/// <summary>
/// Disconnects the DS4 FFB shared memory and stops the FFB thread.
/// </summary>
/// <param name="controller">The DuoController</param>
static void Ds4DisconnectFfb(DUO_CONTROLLER* controller)
{
	// Signal the FFB thread to stop
	if (controller->Ds4FfbStopEvent != NULL)
	{
		SetEvent(controller->Ds4FfbStopEvent);
		if (controller->Ds4FfbThread != NULL)
		{
			WaitForSingleObject(controller->Ds4FfbThread, 1000);
			CloseHandle(controller->Ds4FfbThread);
			controller->Ds4FfbThread = NULL;
		}
		CloseHandle(controller->Ds4FfbStopEvent);
		controller->Ds4FfbStopEvent = NULL;
	}

	// Close the output event
	if (controller->Ds4OutputEvent != NULL)
	{
		CloseHandle(controller->Ds4OutputEvent);
		controller->Ds4OutputEvent = NULL;
	}

	// Unmap the output view
	if (controller->Ds4OutputView != NULL)
	{
		UnmapViewOfFile(controller->Ds4OutputView);
		controller->Ds4OutputView = NULL;
	}

	// Close the output mapping
	if (controller->Ds4OutputMapping != NULL)
	{
		CloseHandle(controller->Ds4OutputMapping);
		controller->Ds4OutputMapping = NULL;
	}
}

/// <summary>
/// Serializes a DS4 input report into a raw HID report and sends it over the pipe.
/// </summary>
/// <param name="controller">The DuoController</param>
/// <param name="state">The DS4 input state to send</param>
/// <returns>Result</returns>
static HRESULT Ds4SendRawInput(DUO_CONTROLLER* controller, const DUO_CONTROLLER_INPUT_REPORT_DS4* state)
{
	if (controller->Ds4InputView == NULL)
	{
		return HRESULT_FROM_WIN32(ERROR_PIPE_NOT_CONNECTED);
	}

	// Build the raw 64-byte DS4 HID report
	BYTE report[DS4_REPORT_SIZE];
	ZeroMemory(report, DS4_REPORT_SIZE);

	report[0] = DS4_INPUT_REPORT_ID;
	report[1] = state->LeftStickX;
	report[2] = state->LeftStickY;
	report[3] = state->RightStickX;
	report[4] = state->RightStickY;

	report[5] = (state->Hat & 0x0F);
	if (state->Square)   report[5] |= 0x10;
	if (state->Cross)    report[5] |= 0x20;
	if (state->Circle)   report[5] |= 0x40;
	if (state->Triangle) report[5] |= 0x80;

	if (state->L1)        report[6] |= 0x01;
	if (state->R1)        report[6] |= 0x02;
	if (state->L2)        report[6] |= 0x04;
	if (state->R2)        report[6] |= 0x08;
	if (state->Share)     report[6] |= 0x10;
	if (state->Options)   report[6] |= 0x20;
	if (state->L3)        report[6] |= 0x40;
	if (state->R3)        report[6] |= 0x80;

	if (state->PS)        report[7] |= 0x01;
	if (state->Touchpad)  report[7] |= 0x02;

	report[8] = state->LeftTrigger;
	report[9] = state->RightTrigger;

	report[12] = 0xFF; // Full battery

	memcpy(&report[13], &state->GyroX, sizeof(INT16));
	memcpy(&report[15], &state->GyroY, sizeof(INT16));
	memcpy(&report[17], &state->GyroZ, sizeof(INT16));
	memcpy(&report[19], &state->AccelX, sizeof(INT16));
	memcpy(&report[21], &state->AccelY, sizeof(INT16));
	memcpy(&report[23], &state->AccelZ, sizeof(INT16));

	report[30] = 0x1A; // Wired - Full battery

	report[35] = state->Touch1Active ? 0 : 0xFF;

	USHORT t1x = min(state->Touch1X, DS4_TOUCHPAD_MAX_X) & 0xFFF;
	USHORT t1y = min(state->Touch1Y, DS4_TOUCHPAD_MAX_Y) & 0xFFF;
	UINT t1 = ((UINT)t1y << 12) | t1x;
	report[36] = (BYTE)(t1 & 0xFF);
	report[37] = (BYTE)((t1 >> 8) & 0xFF);
	report[38] = (BYTE)((t1 >> 16) & 0xFF);

	report[39] = state->Touch2Active ? 1 : 0xFF;

	USHORT t2x = min(state->Touch2X, DS4_TOUCHPAD_MAX_X) & 0xFFF;
	USHORT t2y = min(state->Touch2Y, DS4_TOUCHPAD_MAX_Y) & 0xFFF;
	UINT t2 = ((UINT)t2y << 12) | t2x;
	report[40] = (BYTE)(t2 & 0xFF);
	report[41] = (BYTE)((t2 >> 8) & 0xFF);
	report[42] = (BYTE)((t2 >> 16) & 0xFF);

	// Write the message header + report to shared memory
	BYTE* inputMem = (BYTE*)controller->Ds4InputView;
	inputMem[0] = DS4_INPUT_REPORT_FULL;
	inputMem[1] = DS4_REPORT_SIZE;
	memcpy(&inputMem[MESSAGE_HEADER_LEN], report, DS4_REPORT_SIZE);

	// Signal the input event
	if (!SetEvent(controller->Ds4InputEvent))
	{
		return Ds4Win32ErrorToHresult(GetLastError());
	}

	return S_OK;
}

/// <summary>
/// Creates a new DS4 controller by installing a new DuoController device and connecting to it.
/// </summary>
/// <param name="controller">The DuoController to initialize as DS4</param>
/// <returns>Result</returns>
static HRESULT CreateDs4Controller(DUO_CONTROLLER* controller)
{
	// Install a new DuoController device
	WCHAR instanceId[256];
	HRESULT result = InstallDuoControllerDevice(instanceId, ARRAYSIZE(instanceId));
	if (FAILED(result))
	{
		return result;
	}

	// Store the instance ID
	wcscpy_s(controller->Ds4InstanceId, ARRAYSIZE(controller->Ds4InstanceId), instanceId);

	// Initialize DS4-specific state
	controller->Ds4InputMapping = NULL;
	controller->Ds4OutputMapping = NULL;
	controller->Ds4InputView = NULL;
	controller->Ds4OutputView = NULL;
	controller->Ds4InputEvent = NULL;
	controller->Ds4OutputEvent = NULL;
	controller->Ds4FfbThread = NULL;
	controller->Ds4FfbStopEvent = NULL;
#pragma warning(suppress:4366)
	InitializeCriticalSection(&controller->Ds4Cs);

	// Connect to the input shared memory
	result = Ds4ConnectInput(controller);
	if (FAILED(result))
	{
#pragma warning(suppress:4366)
		DeleteCriticalSection(&controller->Ds4Cs);
		return result;
	}

	// Connect to the FFB output shared memory
	result = Ds4ConnectFfb(controller);
	if (FAILED(result))
	{
		Ds4DisconnectInput(controller);
#pragma warning(suppress:4366)
		DeleteCriticalSection(&controller->Ds4Cs);
		return result;
	}

	return S_OK;
}

/// <summary>
/// Removes a DS4 controller by disconnecting shared memory and removing the device.
/// </summary>
/// <param name="controller">The DuoController to clean up</param>
/// <returns>Result</returns>
static HRESULT RemoveDs4Controller(DUO_CONTROLLER* controller)
{
	// Stop the FFB thread and close the output pipe
	Ds4DisconnectFfb(controller);

	// Close the input pipe
	Ds4DisconnectInput(controller);

	// Delete the critical section
#pragma warning(suppress:4366)
	DeleteCriticalSection(&controller->Ds4Cs);

	// Remove the DuoController device from the system
	return RemoveDuoControllerDevice(controller->Ds4InstanceId);
}

/// <summary>
/// Sends a DS4 input report.
/// </summary>
/// <param name="controller">The DuoController</param>
/// <param name="inputReport">The DS4 input report</param>
/// <returns>Result</returns>
static HRESULT SendDs4Report(DUO_CONTROLLER* controller, DUO_CONTROLLER_INPUT_REPORT_DS4* inputReport)
{
#pragma warning(suppress:4366)
	EnterCriticalSection(&controller->Ds4Cs);
	HRESULT result = Ds4SendRawInput(controller, inputReport);
	if (SUCCEEDED(result))
	{
		controller->LastDs4InputReport = *inputReport;
	}
#pragma warning(suppress:4366)
	LeaveCriticalSection(&controller->Ds4Cs);
	return result;
}

/// <summary>
/// Initializes the DuoController library.
/// </summary>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_Initialize()
{
	// The result
	HRESULT result = S_OK;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// We haven't been initialized yet
	if (!Initialized)
	{
		// Load xboxgipsynthetic.dll or increment its load counter
		HMODULE xboxgipsynthetic = NULL;
		if ((GetModuleHandleExW(0, L"xboxgipsynthetic.dll", &xboxgipsynthetic) && xboxgipsynthetic != NULL) || (xboxgipsynthetic = LoadLibraryExW(L"xboxgipsynthetic.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL)
		{
			// Load cfgmgr32.dll or increment its load counter
			HMODULE cfgmgr32 = NULL;
			if ((GetModuleHandleExW(0, L"cfgmgr32.dll", &cfgmgr32) && cfgmgr32 != NULL) || (cfgmgr32 = LoadLibraryExW(L"cfgmgr32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL)
			{
				// Load ntdll.dll or increment its load counter
				HMODULE ntdll = NULL;
				if ((GetModuleHandleExW(0, L"ntdll.dll", &ntdll) && ntdll != NULL) || (ntdll = LoadLibraryExW(L"ntdll.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL)
				{
					// Import the required functions
					SyntheticController_CreateController = (SyntheticController_CreateController_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_CreateController");
					SyntheticController_SetTargetProcess = (SyntheticController_SetTargetProcess_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_SetTargetProcess");
					SyntheticController_RegisterReportCallback = (SyntheticController_RegisterReportCallback_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_RegisterReportCallback");
					SyntheticController_Connect = (SyntheticController_Connect_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_Connect");
					SyntheticController_SendReport = (SyntheticController_SendReport_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_SendReport");
					SyntheticController_Disconnect = (SyntheticController_Disconnect_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_Disconnect");
					SyntheticController_UnregisterReportCallback = (SyntheticController_UnregisterReportCallback_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_UnregisterReportCallback");
					SyntheticController_RemoveController = (SyntheticController_RemoveController_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_RemoveController");
					SyntheticController_GetDeviceId = (SyntheticController_GetDeviceId_t)GetProcAddress(xboxgipsynthetic, "SyntheticController_GetDeviceId");
					DevSetObjectProperties = (DevSetObjectProperties_t)GetProcAddress(cfgmgr32, "DevSetObjectProperties");
					RtlPublishWnfStateData = (RtlPublishWnfStateData_t)GetProcAddress(ntdll, "RtlPublishWnfStateData");

					// We managed to import the required functions
					if (SyntheticController_CreateController != NULL &&
						SyntheticController_SetTargetProcess != NULL &&
						SyntheticController_RegisterReportCallback &&
						SyntheticController_Connect != NULL &&
						SyntheticController_SendReport != NULL &&
						SyntheticController_Disconnect != NULL &&
						SyntheticController_UnregisterReportCallback != NULL &&
						SyntheticController_RemoveController != NULL &&
						SyntheticController_GetDeviceId != NULL &&
						DevSetObjectProperties != NULL)
					{
						// Mark ourselves as initialized
						Initialized = TRUE;
					}

					// We couldn't import the required SyntheticController_* functions
					else
					{
						// Set the result
						result = E_NOINTERFACE;
					}

					// Something went wrong
					if (result != S_OK)
					{
						// Decrement the load counter of xboxgipsynthetic.dll
						FreeLibrary(xboxgipsynthetic);

						// Clear the imported functions
						SyntheticController_CreateController = NULL;
						SyntheticController_SetTargetProcess = NULL;
						SyntheticController_RegisterReportCallback = NULL;
						SyntheticController_Connect = NULL;
						SyntheticController_SendReport = NULL;
						SyntheticController_Disconnect = NULL;
						SyntheticController_UnregisterReportCallback = NULL;
						SyntheticController_RemoveController = NULL;
						SyntheticController_GetDeviceId = NULL;
					}
				}

				// We couldn't load ntdll.dll
				else
				{
					// Set the result
					result = HRESULT_FROM_WIN32(GetLastError());
				}
			}

			// We couldn't load cfgmgr32.dll
			else
			{
				// Set the result
				result = HRESULT_FROM_WIN32(GetLastError());
			}
		}

		// We couldn't load xboxgipsynthetic.dll
		else
		{
			// Set the result
			result = HRESULT_FROM_WIN32(GetLastError());
		}
	}

	// Return the result
	return result;
}

/// <summary>
/// Uninitializes the DuoController library.
/// </summary>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_Uninitialize()
{
	// The result
	HRESULT result = S_OK;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// We're initialized
	if (Initialized)
	{
		// Iterate all created controllers
		while (Controllers != NULL)
		{
			// Remove the controller
			DuoController_RemoveController(Controllers[0]);
		}

		// Get the xboxgipsynthetic.dll module handle (without incrementing its load counter)
		HMODULE xboxgipsynthetic = NULL;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"xboxgipsynthetic.dll", &xboxgipsynthetic) && xboxgipsynthetic != NULL)
		{
			// Decrement the load counter of xboxgipsynthetic.dll
			FreeLibrary(xboxgipsynthetic);
		}

		// Get the cfgmgr32.dll module handle (without incrementing its load counter)
		HMODULE cfgmgr32 = NULL;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"cfgmgr32.dll", &cfgmgr32) && cfgmgr32 != NULL)
		{
			// Decrement the load counter of cfgmgr32.dll
			FreeLibrary(cfgmgr32);
		}

		// Mark ourselves as uninitialized
		Initialized = FALSE;
	}

	// We aren't initialized
	else
	{
		// Set the result
		result = E_UNEXPECTED;
	}

	// Return the result
	return result;
}

/// <summary>
/// Creates a new Duo controller.
/// </summary>
/// <param name="controllerType">The type of controller to create</param>
/// <param name="vibrationCallback">The vibration report callback</param>
/// <param name="vibrationCallbackContext">The vibration callback context</param>
/// <param name="controller">Receives the created controller</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_CreateController(DUO_CONTROLLER_TYPE controllerType, DuoController_VibrationReportCallback_t vibrationCallback, void* vibrationCallbackContext, void** controller)
{
	// The result
	HRESULT result = S_OK;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// We're initialized
	if (Initialized)
	{
		// We've been given a valid controller output pointer
		if (controller != NULL)
		{
			// Allocate the controller structure
			DUO_CONTROLLER* newController = (DUO_CONTROLLER*)malloc(sizeof(DUO_CONTROLLER));
			
			// We managed to allocate the controller structure
			if (newController != NULL)
			{
				// Initialize the controller structure
				memset(newController, 0, sizeof(DUO_CONTROLLER));

				// Set the controller type
				newController->Type = controllerType;

				// Assign the vibration report callback
				newController->VibrationReportCallback = vibrationCallback;

				// Assign the vibration report callback context
				newController->VibrationReportCallbackContext = vibrationCallbackContext;

				// Initialize type-specific state
				if (controllerType == DuoControllerTypeXbox)
				{
					// Initialize Xbox-specific state
					newController->SyntheticHandle = NULL;
					newController->DeviceQuery = NULL;
					ZeroMemory(&newController->LastXboxInputReport, sizeof(newController->LastXboxInputReport));

					// Create the synthetic controller
#pragma warning(suppress:4366)
					if ((result = SyntheticController_CreateController(0, &newController->SyntheticHandle)) == S_OK)
					{
						// The device ID
						unsigned long long deviceId = 0;

						// Get the device ID
						if (SyntheticController_GetDeviceId(newController->SyntheticHandle, &deviceId) == S_OK)
						{
							// The device ID suffix buffer size
							DWORD deviceIdSuffixBufferSize = 128;

							// Allocate a suitably sized buffer for the device ID suffix
							WCHAR* deviceIdSuffix = (WCHAR*)malloc(deviceIdSuffixBufferSize * sizeof(WCHAR));

							// We managed to allocate a suitably sized device ID buffer
							if (deviceIdSuffix != NULL)
							{
								// Build the device ID suffix (the trailing part of ex. USB\VID_045E&PID_02FF&IG_00\08&00&00008A8ADD9C3C60)
								swprintf_s(deviceIdSuffix, deviceIdSuffixBufferSize, L"&%016llX", deviceId);

								// Build the device query filter
								DEVPROP_FILTER_EXPRESSION ObjectFilter[] =
								{
									{
										DEVPROP_OPERATOR_OR_OPEN
									},
									{
										DEVPROP_OPERATOR_ENDS_WITH_IGNORE_CASE,
										{
											{ DEVPKEY_Device_InstanceId, DEVPROP_STORE_SYSTEM, NULL },
											DEVPROP_TYPE_STRING,
											((ULONG)wcslen(deviceIdSuffix) + 1) * sizeof(WCHAR),
											(BYTE*)deviceIdSuffix
										}
									},
									{
										DEVPROP_OPERATOR_ENDS_WITH_IGNORE_CASE,
										{
											{ DEVPKEY_Device_Parent, DEVPROP_STORE_SYSTEM, NULL },
											DEVPROP_TYPE_STRING,
											((ULONG)wcslen(deviceIdSuffix) + 1) * sizeof(WCHAR),
											(BYTE*)deviceIdSuffix
										}
									},
									{
										DEVPROP_OPERATOR_OR_CLOSE
									}
								};

								// Create the device query
#pragma warning(suppress:4366)
								if ((result = DevCreateObjectQuery(DevObjectTypeDevice, DevQueryFlagUpdateResults, 0, NULL, ARRAYSIZE(ObjectFilter), ObjectFilter, DuoController_DeviceChanged, NULL, &newController->DeviceQuery)) == S_OK)
								{
									// Register the output report callback
									if ((result = SyntheticController_RegisterReportCallback(newController->SyntheticHandle, SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE_CONTROLLER, DuoController_OutputReportReceived, newController)) == S_OK)
									{
										// Connect the controller to the system
										if ((result = SyntheticController_Connect(newController->SyntheticHandle)) == S_OK)
										{
											// Extend the controllers array
											DUO_CONTROLLER** newControllers = (DUO_CONTROLLER**)realloc(Controllers, sizeof(DUO_CONTROLLER*) * (ControllerCount + 1));

											// We managed to extend the controllers array
											if (newControllers)
											{
												// Update the controllers array pointer
												Controllers = newControllers;

												// Store the created controller and increase the count
												Controllers[ControllerCount++] = newController;

												// Return the created controller
												*controller = newController;

												// Wait a second
												Sleep(1000);

												// Re-send the last controller input report
												DuoController_SendReport(newController, &newController->LastXboxInputReport);
											}

											// We failed to extend the controllers array
											else
											{
												// Set the result
												result = E_OUTOFMEMORY;
											}

											// Something went wrong
											if (result != S_OK)
											{
												// Disconnect the controller
												SyntheticController_Disconnect(newController->SyntheticHandle);
											}
										}

										// Something went wrong
										if (result != S_OK)
										{
											// Unregister the output report callback
											SyntheticController_UnregisterReportCallback(newController->SyntheticHandle, SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE_CONTROLLER);
										}
									}

									// Something went wrong
									if (result != S_OK)
									{
										// Close the device query
										DevCloseObjectQuery(newController->DeviceQuery);
									}
								}

								// Free the device ID suffix buffer
								free(deviceIdSuffix);
							}
						}

						// Something went wrong
						if (result != S_OK)
						{
							// Remove the created controller
							SyntheticController_RemoveController(newController->SyntheticHandle);
						}
					}
				}
				else if (controllerType == DuoControllerTypeDs4)
				{
					// Create the DS4 controller
					result = CreateDs4Controller(newController);

					// We created the DS4 controller successfully
					if (SUCCEEDED(result))
					{
						// Extend the controllers array
						DUO_CONTROLLER** newControllers = (DUO_CONTROLLER**)realloc(Controllers, sizeof(DUO_CONTROLLER*) * (ControllerCount + 1));

						// We managed to extend the controllers array
						if (newControllers)
						{
							// Update the controllers array pointer
							Controllers = newControllers;

							// Store the created controller and increase the count
							Controllers[ControllerCount++] = newController;

							// Return the created controller
							*controller = newController;
						}

						// We failed to extend the controllers array
						else
						{
							// Clean up the DS4 controller
							RemoveDs4Controller(newController);

							// Set the result
							result = E_OUTOFMEMORY;
						}
					}
				}

				// Invalid controller type
				else
				{
					result = E_INVALIDARG;
				}

				// Something went wrong
				if (result != S_OK)
				{
					// Free the controller structure
					free(newController);
				}
			}

			// We failed to allocate the controller structure
			else
			{
				// Set the result
				return E_OUTOFMEMORY;
			}
		}

		// We've been given an invalid controller output pointer
		else
		{
			// Set the result
			result = E_INVALIDARG;
		}
	}

	// We aren't initialized
	else
	{
		// Set the result
		result = E_UNEXPECTED;
	}

	// Return the result
	return result;
}

/// <summary>
/// Removes a Duo controller.
/// </summary>
/// <param name="controller">The controller to remove</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_RemoveController(void* controller)
{
	// The result
	HRESULT result = S_OK;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// We're initialized
	if (Initialized)
	{
		// Cast the controller
		DUO_CONTROLLER* duoController = (DUO_CONTROLLER*)controller;

		// Assume the controller argument is invalid
		result = E_INVALIDARG;

		// Iterate the created controllers
		for (DWORD i = 0; i < ControllerCount; i++)
		{
			// We've found the controller we need to remove
			if (Controllers[i] == duoController)
			{
				// Iterate all trailing controllers
				for (DWORD j = i; j < ControllerCount - 1; j++)
				{
					// Shift the controllers forward by one
					Controllers[j] = Controllers[j + 1];
				}

				// Decrease the controller count
				ControllerCount--;

				// We have at least one controller left
				if (ControllerCount > 0)
				{
					// Resize the controllers array
					DUO_CONTROLLER** newControllers = (DUO_CONTROLLER**)realloc(Controllers, sizeof(DUO_CONTROLLER*) * ControllerCount);

					// We managed to resize the controllers array
					if (newControllers != NULL)
					{
						// Update the controllers array pointer
						Controllers = newControllers;
					}
				}

				// We have no controllers left
				else
				{
					// Free the controllers array
					free(Controllers);

					// Clear the controllers array pointer
					Controllers = NULL;
				}

				// Type-specific cleanup
				if (duoController->Type == DuoControllerTypeXbox)
				{
					// Disconnect the controller
					SyntheticController_Disconnect(duoController->SyntheticHandle);

					// Unregister the output report callback
					SyntheticController_UnregisterReportCallback(duoController->SyntheticHandle, SYNTHETIC_CONTROLLER_OUTPUT_REPORT_TYPE_CONTROLLER);

					// Close the device query
					DevCloseObjectQuery(duoController->DeviceQuery);

					// Remove the controller
					SyntheticController_RemoveController(duoController->SyntheticHandle);
				}
				else if (duoController->Type == DuoControllerTypeDs4)
				{
					// Clean up the DS4 controller
					RemoveDs4Controller(duoController);
				}

				// Free the controller structure
				free(duoController);

				// Set the result
				result = S_OK;

				// We've removed the controller
				break;
			}
		}
	}

	// We aren't initialized
	else
	{
		// Set the result
		result = E_UNEXPECTED;
	}

	// Return the result
	return result;
}

/// <summary>
/// Sends an input report to the given Duo controller.
/// </summary>
/// <param name="controller">The controller to send the input report to</param>
/// <param name="inputReport">The input report to send</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_SendReport(void* controller, DUO_CONTROLLER_INPUT_REPORT* inputReport)
{
	// The result
	HRESULT result = S_OK;

	// Initialize the Windows Runtime for the current thread
	InitializeWindowsRuntimeForCurrentThread();

	// We're initialized
	if (Initialized)
	{
		// The arguments are valid
		if (controller != NULL && inputReport != NULL)
		{
			// Cast the controller
			DUO_CONTROLLER* duoController = (DUO_CONTROLLER*)controller;

			// Only Xbox controllers are supported by this function
			if (duoController->Type == DuoControllerTypeXbox)
			{
				// Send the input report
				if ((result = SyntheticController_SendReport(duoController->SyntheticHandle, SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE_CONTROLLER, inputReport, sizeof(DUO_CONTROLLER_INPUT_REPORT))) == S_OK)
				{
					// Create a VKEY input report for the Guide button
					SYNTHETIC_CONTROLLER_VKEY_INPUT_REPORT guideButtonInputReport;

					// Initialize the VKEY input report
					memset(&guideButtonInputReport, 0, sizeof(guideButtonInputReport));

					// Set the Guide button state
					guideButtonInputReport.State = inputReport->Guide;

					// Send the VKEY input report for the Guide button (not mission critical)
					SyntheticController_SendReport(duoController->SyntheticHandle, SYNTHETIC_CONTROLLER_INPUT_REPORT_TYPE_VKEY, &guideButtonInputReport, sizeof(guideButtonInputReport));

					// Update the last controller input report
					duoController->LastXboxInputReport = *inputReport;
				}
			}

			// Wrong report type for this controller
			else
			{
				result = E_INVALIDARG;
			}
		}

		// The arguments are invalid
		else
		{
			// Set the result
			result = E_INVALIDARG;
		}
	}

	// We aren't initialized
	else
	{
		// Set the result
		result = E_UNEXPECTED;
	}

	// Return the result
	return result;
}

/// <summary>
/// Sends a DualShock 4 input report to the given Duo controller.
/// </summary>
/// <param name="controller">The controller to send the input report to</param>
/// <param name="inputReport">The DS4 input report to send</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_SendReportDs4(void* controller, DUO_CONTROLLER_INPUT_REPORT_DS4* inputReport)
{
	// The result
	HRESULT result = S_OK;

	// We're initialized
	if (Initialized)
	{
		// The arguments are valid
		if (controller != NULL && inputReport != NULL)
		{
			// Cast the controller
			DUO_CONTROLLER* duoController = (DUO_CONTROLLER*)controller;

			// Only DS4 controllers are supported by this function
			if (duoController->Type == DuoControllerTypeDs4)
			{
				// Send the DS4 input report
				result = SendDs4Report(duoController, inputReport);
			}

			// Wrong report type for this controller
			else
			{
				result = E_INVALIDARG;
			}
		}

		// The arguments are invalid
		else
		{
			result = E_INVALIDARG;
		}
	}

	// We aren't initialized
	else
	{
		result = E_UNEXPECTED;
	}

	return result;
}

/// <summary>
/// The library entry point.
/// </summary>
/// <param name="hModule">Module handle</param>
/// <param name="ul_reason_for_call">Event type</param>
/// <param name="lpReserved">Reserved</param>
/// <returns>Result</returns>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	// Unreferenced parameters
	(void)hModule;
	(void)lpReserved;

	// We're attaching to the process
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		// Get our session ID
		ProcessIdToSessionId(GetCurrentProcessId(), &SessionId);
	}

	// Return success
	return TRUE;
}
