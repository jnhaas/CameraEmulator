# CameraEmulator

A Windows 11 virtual webcam that streams a static image, built on the Media Foundation
virtual camera API (`MFCreateVirtualCamera`). It is meant to feed the Android SDK emulator
(`-camera-back webcam0`), but any camera app sees it.

## How it works

| Component | Role |
|---|---|
| `CameraEmulatorSource.dll` | COM media source. Windows' **Frame Server** service loads it and pulls frames from it. |
| `CameraEmulator.exe` | Publishes the image and registers the virtual camera for as long as it runs. |
| `SourceTest.exe` | Test harness: loads the DLL directly and dumps one frame to a BMP. |

The image is read from `C:\ProgramData\CameraEmulator\image.png`. Frame Server runs as
`LOCAL SERVICE` and cannot read your profile, so the app copies the image there. The source
checks the file once per second, so replacing it swaps the picture live. Any format WIC can
decode works (PNG, JPEG, BMP...). The image is letterboxed, and transparent areas render black.
If no image exists, the camera shows color bars.

Formats offered: NV12 and RGB32, each at 1280×720 and 640×480, 30 fps.

## Build

Requires Visual Studio 2022+ with the C++ workload and Windows SDK 10.0.22000+.

```bat
cmake -S . -B build
cmake --build build --config Release
```

## Install (once, elevated)

Frame Server is a service, so the COM class must be registered machine-wide. Run this from an
**administrator** prompt:

```bat
regsvr32 build\Release\CameraEmulatorSource.dll
```

The DLL stays loaded from that path. Rebuilding while a camera is in use fails because the file
is locked: stop `CameraEmulator.exe` first, or restart the `FrameServer` service. Unregister with
`regsvr32 /u`.

## Run

```bat
build\Release\CameraEmulator.exe path\to\image.png
```

The camera, named **CameraEmulator**, exists until you press Ctrl+C. To change the image while it runs:

```bat
build\Release\CameraEmulator.exe --set-image path\to\other.png
```

## Use with the Android emulator

```bat
emulator -webcam-list
emulator -avd <your_avd> -camera-back webcam0
```

Use the `webcamN` name that `-webcam-list` reports for CameraEmulator. You can also set
`hw.camera.back=webcam0` in the AVD's `config.ini`.

## Troubleshooting

- **Camera not listed anywhere:** check Settings → Privacy & security → Camera, and allow
  desktop apps to access the camera.
- **`IMFVirtualCamera::Start` fails:** the DLL is not registered, or it is in a folder
  `LOCAL SERVICE` cannot read.
- **Logs:** the source writes `[CameraEmulator]` lines with `OutputDebugString`. View them in
  Sysinternals DebugView with *Capture Global Win32* enabled, run as administrator.
- **Test without Frame Server:**
  `build\Release\SourceTest.exe build\Release\CameraEmulatorSource.dll frame.bmp`
