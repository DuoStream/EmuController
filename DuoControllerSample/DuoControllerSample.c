#include "../DuoController/DuoController.h"
#include <stdio.h>
#include <stdlib.h>

typedef HRESULT (*DuoController_Initialize_t)();
typedef HRESULT (*DuoController_Uninitialize_t)();
typedef HRESULT(*DuoController_CreateController_t)(DUO_CONTROLLER_TYPE controllerType, DuoController_VibrationReportCallback_t vibrationCallback, void* vibrationCallbackContext, void** controller);
typedef HRESULT(*DuoController_RemoveController_t)(void* controller);
typedef HRESULT(*DuoController_SendReport_t)(void* controller, DUO_CONTROLLER_INPUT_REPORT* inputReport);
typedef HRESULT(*DuoController_SendReportDs4_t)(void* controller, DUO_CONTROLLER_INPUT_REPORT_DS4* inputReport);

typedef enum _SAMPLE_CONTROLLER_TYPE
{
	SampleControllerTypeXbox,
	SampleControllerTypeDs4
} SAMPLE_CONTROLLER_TYPE;

/// <summary>
/// Receives vibration reports from the Duo controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="smallMotorSpeed">The speed of the small motor</param>
/// <param name="largeMotorSpeed">The speed of the large motor</param>
/// <param name="context">The context</param>
void VibrationReportCallback(void* controller, DUO_CONTROLLER_FORCE_FEEDBACK_REPORT* report, void* context)
{
	// Log the vibration report
	wprintf(L"Vibration report received: LeftMotor=%d, RightMotor=%d, Duration=%d, Delay=%d, Repeat=%d\n", report->LeftMotor, report->RightMotor, report->Duration, report->Delay, report->Repeat);
}

/// <summary>
/// Runs the Xbox input report loop.
/// </summary>
static void RunXboxLoop(void* controller, DuoController_SendReport_t sendReport)
{
	DUO_CONTROLLER_INPUT_REPORT inputReport;
	memset(&inputReport, 0, sizeof(inputReport));
	char lineBuffer[128];

	while (fgets(lineBuffer, sizeof(lineBuffer), stdin) != NULL)
	{
		if (strcmp(lineBuffer, "exit\n") == 0)
			break;

		if (strcmp(lineBuffer, "reset\n") == 0)
		{
			memset(&inputReport, 0, sizeof(inputReport));
			HRESULT hr = sendReport(controller, &inputReport);
			wprintf(SUCCEEDED(hr) ? L"Report sent\n" : L"Send failed: 0x%08X\n", hr);
			continue;
		}

		char buttonName[64];
		int buttonState = 0;
		if (sscanf_s(lineBuffer, "%63s %d", buttonName, (unsigned)_countof(buttonName), &buttonState) != 2)
		{
			wprintf(L"Usage: fieldname value\n");
			continue;
		}

		if (strcmp(buttonName, "Sync") == 0)               inputReport.Sync = buttonState;
		else if (strcmp(buttonName, "Guide") == 0)          inputReport.Guide = buttonState;
		else if (strcmp(buttonName, "Start") == 0)          inputReport.Start = buttonState;
		else if (strcmp(buttonName, "Back") == 0)           inputReport.Back = buttonState;
		else if (strcmp(buttonName, "A") == 0)              inputReport.A = buttonState;
		else if (strcmp(buttonName, "B") == 0)              inputReport.B = buttonState;
		else if (strcmp(buttonName, "X") == 0)              inputReport.X = buttonState;
		else if (strcmp(buttonName, "Y") == 0)              inputReport.Y = buttonState;
		else if (strcmp(buttonName, "DPadUp") == 0)         inputReport.DPadUp = buttonState;
		else if (strcmp(buttonName, "DPadDown") == 0)       inputReport.DPadDown = buttonState;
		else if (strcmp(buttonName, "DPadLeft") == 0)       inputReport.DPadLeft = buttonState;
		else if (strcmp(buttonName, "DPadRight") == 0)      inputReport.DPadRight = buttonState;
		else if (strcmp(buttonName, "LeftBumper") == 0)     inputReport.LeftBumper = buttonState;
		else if (strcmp(buttonName, "RightBumper") == 0)    inputReport.RightBumper = buttonState;
		else if (strcmp(buttonName, "LeftStick") == 0)      inputReport.LeftStick = buttonState;
		else if (strcmp(buttonName, "RightStick") == 0)     inputReport.RightStick = buttonState;
		else if (strcmp(buttonName, "LeftTrigger") == 0)    inputReport.LeftTrigger = (BYTE)buttonState;
		else if (strcmp(buttonName, "RightTrigger") == 0)   inputReport.RightTrigger = (BYTE)buttonState;
		else if (strcmp(buttonName, "LeftStickHorizontal") == 0)  inputReport.LeftStickHorizontal = (SHORT)buttonState;
		else if (strcmp(buttonName, "LeftStickVertical") == 0)    inputReport.LeftStickVertical = (SHORT)buttonState;
		else if (strcmp(buttonName, "RightStickHorizontal") == 0) inputReport.RightStickHorizontal = (SHORT)buttonState;
		else if (strcmp(buttonName, "RightStickVertical") == 0)   inputReport.RightStickVertical = (SHORT)buttonState;
		else
		{
			wprintf(L"Unknown Xbox field: %s\n", buttonName);
			continue;
		}

		wprintf(L"Sending report in 3 seconds...\n");
		Sleep(3000);
		HRESULT hr = sendReport(controller, &inputReport);
		wprintf(SUCCEEDED(hr) ? L"Report sent\n" : L"Send failed: 0x%08X\n", hr);
	}
}

/// <summary>
/// Runs the DS4 input report loop.
/// </summary>
static void RunDs4Loop(void* controller, DuoController_SendReportDs4_t sendReportDs4)
{
	DUO_CONTROLLER_INPUT_REPORT_DS4 inputReport;
	memset(&inputReport, 0, sizeof(inputReport));
	char lineBuffer[128];

	while (fgets(lineBuffer, sizeof(lineBuffer), stdin) != NULL)
	{
		if (strcmp(lineBuffer, "exit\n") == 0)
			break;

		if (strcmp(lineBuffer, "reset\n") == 0)
		{
			memset(&inputReport, 0, sizeof(inputReport));
			HRESULT hr = sendReportDs4(controller, &inputReport);
			wprintf(SUCCEEDED(hr) ? L"Report sent\n" : L"Send failed: 0x%08X\n", hr);
			continue;
		}

		char fieldName[64];
		int value = 0;
		if (sscanf_s(lineBuffer, "%63s %d", fieldName, (unsigned)_countof(fieldName), &value) != 2)
		{
			wprintf(L"Usage: fieldname value\n");
			continue;
		}

		if (strcmp(fieldName, "LeftStickX") == 0)      inputReport.LeftStickX = (BYTE)value;
		else if (strcmp(fieldName, "LeftStickY") == 0) inputReport.LeftStickY = (BYTE)value;
		else if (strcmp(fieldName, "RightStickX") == 0) inputReport.RightStickX = (BYTE)value;
		else if (strcmp(fieldName, "RightStickY") == 0) inputReport.RightStickY = (BYTE)value;
		else if (strcmp(fieldName, "Hat") == 0)         inputReport.Hat = (BYTE)value;
		else if (strcmp(fieldName, "Square") == 0)      inputReport.Square = (BYTE)value;
		else if (strcmp(fieldName, "Cross") == 0)       inputReport.Cross = (BYTE)value;
		else if (strcmp(fieldName, "Circle") == 0)      inputReport.Circle = (BYTE)value;
		else if (strcmp(fieldName, "Triangle") == 0)    inputReport.Triangle = (BYTE)value;
		else if (strcmp(fieldName, "L1") == 0)          inputReport.L1 = (BYTE)value;
		else if (strcmp(fieldName, "R1") == 0)          inputReport.R1 = (BYTE)value;
		else if (strcmp(fieldName, "L2") == 0)          inputReport.L2 = (BYTE)value;
		else if (strcmp(fieldName, "R2") == 0)          inputReport.R2 = (BYTE)value;
		else if (strcmp(fieldName, "Share") == 0)       inputReport.Share = (BYTE)value;
		else if (strcmp(fieldName, "Options") == 0)     inputReport.Options = (BYTE)value;
		else if (strcmp(fieldName, "L3") == 0)          inputReport.L3 = (BYTE)value;
		else if (strcmp(fieldName, "R3") == 0)          inputReport.R3 = (BYTE)value;
		else if (strcmp(fieldName, "PS") == 0)          inputReport.PS = (BYTE)value;
		else if (strcmp(fieldName, "Touchpad") == 0)    inputReport.Touchpad = (BYTE)value;
		else if (strcmp(fieldName, "LeftTrigger") == 0) inputReport.LeftTrigger = (BYTE)value;
		else if (strcmp(fieldName, "RightTrigger") == 0) inputReport.RightTrigger = (BYTE)value;
		else if (strcmp(fieldName, "GyroX") == 0)       inputReport.GyroX = (INT16)value;
		else if (strcmp(fieldName, "GyroY") == 0)       inputReport.GyroY = (INT16)value;
		else if (strcmp(fieldName, "GyroZ") == 0)       inputReport.GyroZ = (INT16)value;
		else if (strcmp(fieldName, "AccelX") == 0)      inputReport.AccelX = (INT16)value;
		else if (strcmp(fieldName, "AccelY") == 0)      inputReport.AccelY = (INT16)value;
		else if (strcmp(fieldName, "AccelZ") == 0)      inputReport.AccelZ = (INT16)value;
		else if (strcmp(fieldName, "Touch1Active") == 0) inputReport.Touch1Active = (BYTE)value;
		else if (strcmp(fieldName, "Touch1X") == 0)     inputReport.Touch1X = (USHORT)value;
		else if (strcmp(fieldName, "Touch1Y") == 0)     inputReport.Touch1Y = (USHORT)value;
		else if (strcmp(fieldName, "Touch2Active") == 0) inputReport.Touch2Active = (BYTE)value;
		else if (strcmp(fieldName, "Touch2X") == 0)     inputReport.Touch2X = (USHORT)value;
		else if (strcmp(fieldName, "Touch2Y") == 0)     inputReport.Touch2Y = (USHORT)value;
		else
		{
			wprintf(L"Unknown DS4 field: %s\n", fieldName);
			continue;
		}

		wprintf(L"Sending report in 3 seconds...\n");
		Sleep(3000);
		HRESULT hr = sendReportDs4(controller, &inputReport);
		wprintf(SUCCEEDED(hr) ? L"Report sent\n" : L"Send failed: 0x%08X\n", hr);
	}
}

/// <summary>
/// The sample entry point.
/// </summary>
int main(int argc, char* argv[])
{
	HRESULT result = S_OK;

	wprintf(L"DuoController Sample Application\n");
	wprintf(L"Select controller type: 1 = Xbox, 2 = DS4\n");
	wprintf(L"Choice: ");

	char choiceBuffer[16];
	SAMPLE_CONTROLLER_TYPE sampleType = SampleControllerTypeXbox;
	if (fgets(choiceBuffer, sizeof(choiceBuffer), stdin) != NULL)
	{
		int choice = atoi(choiceBuffer);
		if (choice == 2)
			sampleType = SampleControllerTypeDs4;
	}

	HMODULE duoController = LoadLibraryW(L"DuoController\\DuoController.dll");
	if (duoController == NULL)
	{
		wprintf(L"Failed to load DuoController.dll: 0x%08X\n", HRESULT_FROM_WIN32(GetLastError()));
		return (int)HRESULT_FROM_WIN32(GetLastError());
	}

	DuoController_Initialize_t DuoController_Initialize_Dynamic = (DuoController_Initialize_t)GetProcAddress(duoController, "DuoController_Initialize");
	DuoController_Uninitialize_t DuoController_Uninitialize_Dynamic = (DuoController_Uninitialize_t)GetProcAddress(duoController, "DuoController_Uninitialize");
	DuoController_CreateController_t DuoController_CreateController_Dynamic = (DuoController_CreateController_t)GetProcAddress(duoController, "DuoController_CreateController");
	DuoController_RemoveController_t DuoController_RemoveController_Dynamic = (DuoController_RemoveController_t)GetProcAddress(duoController, "DuoController_RemoveController");
	DuoController_SendReport_t DuoController_SendReport_Dynamic = (DuoController_SendReport_t)GetProcAddress(duoController, "DuoController_SendReport");
	DuoController_SendReportDs4_t DuoController_SendReportDs4_Dynamic = (DuoController_SendReportDs4_t)GetProcAddress(duoController, "DuoController_SendReportDs4");

	if (DuoController_Initialize_Dynamic == NULL ||
		DuoController_Uninitialize_Dynamic == NULL ||
		DuoController_CreateController_Dynamic == NULL ||
		DuoController_RemoveController_Dynamic == NULL ||
		(sampleType == SampleControllerTypeXbox && DuoController_SendReport_Dynamic == NULL) ||
		(sampleType == SampleControllerTypeDs4 && DuoController_SendReportDs4_Dynamic == NULL))
	{
		wprintf(L"Failed to resolve required DuoController functions\n");
		FreeLibrary(duoController);
		return E_FAIL;
	}

	if (FAILED(result = DuoController_Initialize_Dynamic()))
	{
		wprintf(L"Failed to initialize DuoController library: 0x%08X\n", result);
		FreeLibrary(duoController);
		return result;
	}

	void* controller = NULL;
	result = DuoController_CreateController_Dynamic(
		sampleType == SampleControllerTypeDs4 ? DuoControllerTypeDs4 : DuoControllerTypeXbox,
		VibrationReportCallback, NULL, &controller);

	if (FAILED(result))
	{
		wprintf(L"Failed to create Duo controller: 0x%08X\n", result);
		DuoController_Uninitialize_Dynamic();
		FreeLibrary(duoController);
		return result;
	}

	if (sampleType == SampleControllerTypeDs4)
		RunDs4Loop(controller, DuoController_SendReportDs4_Dynamic);
	else
		RunXboxLoop(controller, DuoController_SendReport_Dynamic);

	DuoController_RemoveController_Dynamic(controller);
	DuoController_Uninitialize_Dynamic();
	FreeLibrary(duoController);

	return S_OK;
}