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
| `ReaderTest.exe` | Test harness: reads the running virtual camera at a given size, the way the Android emulator does, and dumps a BMP. |

The image is read from `C:\ProgramData\CameraEmulator\image.png`. Frame Server runs as
`LOCAL SERVICE` and cannot read your profile, so the app copies the image there. The source
checks the file once per second, so replacing it swaps the picture live. Any format WIC can
decode works (PNG, JPEG, BMP...). The image is letterboxed according to the layout (see below),
and transparent areas render black.
If no image exists, the camera shows color bars.

Formats offered: NV12 and RGB32 at 30 fps, in 1280×720, 1280×960, 640×480, 352×288, 320×240 and
176×144. These are exactly the sizes the Android emulator requests, so Media Foundation never
rescales the frame (rescaling to a different aspect ratio adds black bars).

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

The DLL is loaded from that path. Unregister with `regsvr32 /u`.

### Updating the DLL

The Frame Server service keeps the DLL loaded after the first client uses the camera, so a
rebuild is not picked up until the service restarts:

1. Stop `CameraEmulator.exe`.
2. If the link step fails because the DLL is locked, rename `build\Release\CameraEmulatorSource.dll`
   (Windows allows renaming a loaded DLL) and build again.
3. From an **administrator** prompt, run `net stop FrameServer`. It restarts on demand.

## Run

```bat
build\Release\CameraEmulator.exe path\to\image.png
```

The camera, named **CameraEmulator**, exists until you press Ctrl+C. To change the image or the
layout while it runs:

```bat
build\Release\CameraEmulator.exe --update path\to\other.png
build\Release\CameraEmulator.exe --update --layout fit
```

### Layouts

The layout is saved in `C:\ProgramData\CameraEmulator\settings.ini` and applied within a second.

- **`android`** (default): when the virtual phone is in portrait, the Android emulator rotates
  webcam frames by keeping only a left-anchored `H²/W × H` region and scaling it up to the whole
  picture. For a 1280×960 frame, that's the left 720 columns; everything to the right is
  discarded. This layout fits the image into that region, so the whole image shows up in the
  emulated camera.
- **`fit`**: letterboxes the image into the whole frame. Use this for regular desktop apps such as
  the Windows Camera app. In those apps, `android` shows the image squeezed into the left part of
  the frame.

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
- **Image cropped or zoomed in the emulator:** check the layout is `android`, and that the AVD
  really uses CameraEmulator. `hw.camera.back=webcamN` in `config.ini` refers to the
  `-webcam-list` order, which changes when other cameras appear.
- **Logs:** the source writes `[CameraEmulator]` lines with `OutputDebugString`. View them in
  Sysinternals DebugView with *Capture Global Win32* enabled, run as administrator. Lines from the
  `CameraEmulator.exe` process also go to `C:\ProgramData\CameraEmulator\source.log`.
- **Test without Frame Server:**
  `build\Release\SourceTest.exe build\Release\CameraEmulatorSource.dll frame.bmp [width height]`
