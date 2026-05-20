<p align="center">
  <img src="https://img.shields.io/badge/Windows-10+-0078D6?style=for-the-badge&logo=windows&logoColor=white" />
  <img src="https://img.shields.io/badge/UMDF-2.0-7F52FF?style=for-the-badge&logo=windows&logoColor=white" />
  <img src="https://img.shields.io/badge/x64-00599C?style=for-the-badge&logo=c&logoColor=white" />
  <img src="https://img.shields.io/badge/License-Apache%202.0-yellow?style=for-the-badge" />
</p>

## Overview

**DuoController** is a pure userspace virtual gamepad library and UMDF driver for Windows that emulates **DualShock 4** and **Xbox One** controllers entirely from user mode.

It is a spiritual successor to [ViGEmBus](https://github.com/ViGEm/ViGEmBus), the now-abandoned KMDF-based kernel driver, reimagined using Microsoft's **User-Mode Driver Framework (UMDF)**.

## Why Userspace?

Microsoft is actively steering the Windows driver ecosystem away from kernel-mode and toward user-mode drivers.

The reasoning is clear:

| Concern | Kernel Driver (KMDF) | User Driver (UMDF) |
|---|---|---|
| **System Stability** | A crash takes down the entire OS | A crash takes down only the driver process |
| **Attestation Signing** | Required (EV certificate, expensive) | **Not required** |
| **Development Complexity** | High - kernel debugging, WinDbg, VM setup | Low - standard user-mode tooling |
| **Security Surface** | Ring 0 - full system access | Ring 3 - sandboxed, limited privileges |
| **Distribution** | Signing portal, HLK testing | Standard DLL deployment |

### The Signing Problem

Kernel drivers on modern Windows **must** be submitted to the Windows Hardware Dev Center, pass through attestation signing, and have to be pre-signed by an Extended Validation (EV) code signing certificate.

This process costs hundreds of dollars, requires a registered legal entity that fulfills Microsoft's requirements, and takes weeks of turnaround.

UMDF drivers require **no such signing**. A standard code signing certificate (or even self-signing for development or small-scale publishing) is sufficient.

This makes DuoController dramatically more accessible to:

- **Independent developers**
- **Small studios**

## Features

- **DualShock 4 Emulation** - Via HID minidriver
- **Xbox One Emulation** - Via `xboxgipsynthetic.dll`
- **Touchpad Support** - Full touchpad support for DualShock 4
- **Gyroscope / Accelerometer Support** - Full gyroscope / accelerometer support for DualShock 4
- **Force Feedback** - Full vibration / rumble callback support for both controller types
- **Session Isolation** - Isolates gamepads to the current session ID for multi-session and RDP environments
- **Minimal Footprint** - A single DLL / INF pair, no kernel components

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Your Application                         │
│                                                             │
│  DuoController_Initialize()                                 │
│  DuoController_CreateController(Ds4 | Xbox)                 │
│  DuoController_SendReport() / DuoController_SendReportDs4() │
│  DuoController_RemoveController()                           │
│  DuoController_Uninitialize()                               │
└────────────────────┬────────────────────────────────────────┘
                     │
     ┌───────────────┴───────────────┐
     ▼                               ▼
┌─────────────────┐          ┌──────────────────┐
│  DS4 Path       │          │  Xbox Path       │
│                 │          │                  │
│  Shared Mem     │          │ xboxgipsynthetic │
│  ────────────── │          │ ──────────────── │
│  DuoController  │          │ (System API)     │
│  UMDF Driver    │          │                  │
│  (HID Miniport) │          │                  │
└──────┬──────────┘          └────────┬─────────┘
       │                              │
       ▼                              ▼
┌──────────────────────────────────────────────┐
│            Windows Game Input API            │
│      (GameInput / XInput / DirectInput)      │
└──────────────────────────────────────────────┘
```

## API

```c
// Initialize the library
HRESULT DuoController_Initialize();

// Create a virtual controller
HRESULT DuoController_CreateController(
    DUO_CONTROLLER_TYPE controllerType,    // DuoControllerTypeXbox or DuoControllerTypeDs4
    DuoController_VibrationReportCallback_t vibrationCallback,
    void* vibrationCallbackContext,
    void** controller
);

// Send Xbox One input report
HRESULT DuoController_SendReport(
    void* controller,
    DUO_CONTROLLER_INPUT_REPORT* inputReport
);

// Send DualShock 4 input report
HRESULT DuoController_SendReportDs4(
    void* controller,
    DUO_CONTROLLER_INPUT_REPORT_DS4* inputReport
);

// Remove a virtual controller
HRESULT DuoController_RemoveController(void* controller);

// Uninitialize the library
HRESULT DuoController_Uninitialize();
```

## Getting Started

### Prerequisites

- Windows 10 or later (x64)
- Visual Studio 2026
- Windows Driver Kit (WDK, Nu-Get preferred)

### Build

```powershell
# Open DuoController.sln in Visual Studio and build for Release x64
```

## Projects

| Project | Description |
|---|---|
| `DuoController` | The UMDF driver DLL and library - implements the HID minidriver, shared memory server, and client API |
| `DuoControllerSample` | A minimal C sample showing library usage with both Xbox and DS4 controller types |

## License

Apache 2.0 © 2026 Black-Seraph