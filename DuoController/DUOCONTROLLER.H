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

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <roapi.h>

/// <summary>
/// Controller types supported by DuoController.
/// </summary>
typedef enum _DUO_CONTROLLER_TYPE
{
	/// <summary>
	/// Virtual Xbox One controller (uses xboxgipsynthetic.dll).
	/// </summary>
	DuoControllerTypeXbox = 0,

	/// <summary>
	/// Virtual DualShock 4 controller (uses DuoController driver).
	/// </summary>
	DuoControllerTypeDs4 = 1,
} DUO_CONTROLLER_TYPE;

/// <summary>
/// Defines the populated fields in a force feedback report.
/// </summary>
typedef enum _SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAGS
{
	SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_RIGHT_MOTOR_VALID = 0x1,
	SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_LEFT_MOTOR_VALID = 0x2,
	SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_RIGHT_TRIGGER_VALID = 0x4,
	SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_LEFT_TRIGGER_VALID = 0x8,
} SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAGS;

#pragma pack(push, 1)

/// <summary>
/// The Xbox controller input report structure.
/// </summary>
typedef struct _DUO_CONTROLLER_INPUT_REPORT
{
	UINT8 Sync : 1; // Unused
	UINT8 Guide : 1;
	UINT8 Start : 1;
	UINT8 Back : 1;

	UINT8 A : 1;
	UINT8 B : 1;
	UINT8 X : 1;
	UINT8 Y : 1;

	UINT8 DPadUp : 1;
	UINT8 DPadDown : 1;
	UINT8 DPadLeft : 1;
	UINT8 DPadRight : 1;

	UINT8 LeftBumper : 1;
	UINT8 RightBumper : 1;
	UINT8 LeftStick : 1;
	UINT8 RightStick : 1;

	UINT16 LeftTrigger; // Analog 0-65535
	UINT16 RightTrigger; // Analog 0-65535

	INT16 LeftStickHorizontal; // -32768 (left) to 32767 (right)
	INT16 LeftStickVertical;   // -32768 (down) to 32767 (up)
	INT16 RightStickHorizontal; // -32768 (left) to 32767 (right)
	INT16 RightStickVertical;   // -32768 (down) to 32767 (up)
} DUO_CONTROLLER_INPUT_REPORT;

/// <summary>
/// The DualShock 4 controller input report structure.
/// </summary>
typedef struct _DUO_CONTROLLER_INPUT_REPORT_DS4
{
	UINT8 LeftStickX;
	UINT8 LeftStickY;
	UINT8 RightStickX;
	UINT8 RightStickY;

	UINT8 LeftTrigger;
	UINT8 RightTrigger;

	UINT8 Hat; // 0 = Up, 1 = Up-Right, 2 = Right, 3 = Down-Right, 4 = Down, 5 = Down-Left, 6 = Left, 7 = Up-Left, 8 = Neutral

	UINT8 Square : 1;
	UINT8 Cross : 1;
	UINT8 Circle : 1;
	UINT8 Triangle : 1;
	UINT8 L1 : 1;
	UINT8 R1 : 1;
	UINT8 L2 : 1;
	UINT8 R2 : 1;
	UINT8 Share : 1;
	UINT8 Options : 1;
	UINT8 L3 : 1;
	UINT8 R3 : 1;
	UINT8 PS : 1;
	UINT8 Touchpad : 1;

	INT16 GyroX;
	INT16 GyroY;
	INT16 GyroZ;

	INT16 AccelX;
	INT16 AccelY;
	INT16 AccelZ;

	UINT8 Touch1Active;
	USHORT Touch1X;
	USHORT Touch1Y;
	UINT8 Touch2Active;
	USHORT Touch2X;
	USHORT Touch2Y;
} DUO_CONTROLLER_INPUT_REPORT_DS4;

/// <summary>
/// The controller force feedback report structure.
/// </summary>
typedef struct _DUO_CONTROLLER_FORCE_FEEDBACK_REPORT
{
	UINT8 Flags; // SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_RIGHT_MOTOR_VALID | SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_LEFT_MOTOR_VALID | SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_RIGHT_TRIGGER_VALID | SYNTHETIC_CONTROLLER_OUTPUT_REPORT_FLAG_LEFT_TRIGGER_VALID
	UINT8 LeftTrigger; // 0-255
	UINT8 RightTrigger; // 0-255
	UINT8 LeftMotor; // 0-255
	UINT8 RightMotor; // 0-255
	UINT8 Duration; // 0-255 (246 on test capture, must be 255 when combined with Delay)
	UINT8 Delay; // 0-255 (9 on test capture, must be 255 when combined with Duration)
	UINT8 Repeat; // 0 or 1 (0 on test capture)
	UINT8 PulsePeriod; // 0 on test capture
	UINT8 NumberOfPulses; // 235 on test capture
} DUO_CONTROLLER_FORCE_FEEDBACK_REPORT;

#pragma pack(pop)

/// <summary>
/// Receives vibration data from a Duo controller.
/// </summary>
/// <param name="controller">The controller</param>
/// <param name="report">The force feedback report</param>
/// <param name="context">The context</param>
typedef void (*DuoController_VibrationReportCallback_t)(void* controller, DUO_CONTROLLER_FORCE_FEEDBACK_REPORT* report, void* context);

/// <summary>
/// Initializes the DuoController library.
/// </summary>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_Initialize();

/// <summary>
/// Uninitializes the DuoController library.
/// </summary>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_Uninitialize();

/// <summary>
/// Creates a new Duo controller.
/// </summary>
/// <param name="controllerType">The type of controller to create</param>
/// <param name="vibrationCallback">The vibration report callback</param>
/// <param name="vibrationCallbackContext">The vibration callback context</param>
/// <param name="controller">Receives the created controller</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_CreateController(DUO_CONTROLLER_TYPE controllerType, DuoController_VibrationReportCallback_t vibrationCallback, void* vibrationCallbackContext, void** controller);

/// <summary>
/// Removes a Duo controller.
/// </summary>
/// <param name="controller">The controller to remove</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_RemoveController(void* controller);

/// <summary>
/// Sends an Xbox input report to the given Duo controller.
/// </summary>
/// <param name="controller">The controller to send the input report to</param>
/// <param name="inputReport">The input report to send</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_SendReport(void* controller, DUO_CONTROLLER_INPUT_REPORT* inputReport);

/// <summary>
/// Sends a DualShock 4 input report to the given Duo controller.
/// </summary>
/// <param name="controller">The controller to send the input report to</param>
/// <param name="inputReport">The DS4 input report to send</param>
/// <returns>Result</returns>
HRESULT WINAPI DuoController_SendReportDs4(void* controller, DUO_CONTROLLER_INPUT_REPORT_DS4* inputReport);
