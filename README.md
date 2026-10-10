# CameraEmulator

A Windows 11 virtual webcam that streams a static image, built on the Media Foundation
virtual camera API (`MFCreateVirtualCamera`). It is meant to feed the Android SDK emulator
(`-camera-back webcam0`), but any camera app sees it.

## How it works

| Component | Role |
|---|---|
| `CameraEmulatorSource.dll` | COM media source. Windows' **Frame Server** service loads it and pulls frames from it. |
| `CameraEmulatorTray.exe` | Notification area (system tray) app with no console: all the features below from the icon's menu. |
| `CameraEmulator.exe` | Command-line version, for scripts: publishes the image (or captures a zone of the screen), installs or exposes the camera. |
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

## Tray app

```bat
build\Release\CameraEmulatorTray.exe
```

A camera icon appears in the notification area. It shows a red dot while a screen zone is being
broadcast. Windows 11 puts new icons in the overflow area (the **^** arrow); to keep it visible,
drag it onto the taskbar, or enable it in Settings → Personalization → Taskbar → Other system
tray icons. Left- or right-click the icon for the menu:

- **Choose image…**: the picture shown by the camera.
- **Broadcast screen zone**: drag a rectangle (*Select zone…*), reuse the *Last zone*, or pick a
  centered size. Drag the red frame to move the zone.
- **Stop broadcast**: also available from anywhere with the stop shortcut (Ctrl+Alt+X by
  default; set `stopKey=` in the `[screen]` section of `settings.ini` to change it).
- **Layout**: *Android emulator (portrait)* or *Fit (desktop apps)*.
- **Keep the camera when this app is closed**: installs the camera permanently, the same as
  `--install`. When unchecked, the camera exists only while the tray app runs.
- **Start with Windows**: adds the tray app to your startup programs (`HKCU\…\Run`).
- **Open settings folder**, **Exit**.

Only one instance runs at a time. Diagnostics go to `C:\ProgramData\CameraEmulator\tray.log`.
The window also accepts the menu commands as `WM_COMMAND` messages (IDs in
`src/tray/TrayApp.cpp`), which is handy for scripts.

## Command line

### Permanent camera (recommended)

```bat
build\Release\CameraEmulator.exe --install path\to\image.png
```

This installs the camera, named **CameraEmulator**, for your user account. No administrator
rights are needed. The camera stays available across restarts and reboots until you run
`--uninstall`, and it shows the image without `CameraEmulator.exe` running, because frames come
from the source DLL inside Frame Server. The Android emulator therefore always finds it. Without
it, starting an AVD whose camera is set to a missing webcam makes the Android camera app crash.

With the camera installed, `CameraEmulator.exe` only changes settings or broadcasts the screen.
It never creates a second camera:

```bat
build\Release\CameraEmulator.exe path\to\other.png
build\Release\CameraEmulator.exe --layout fit
build\Release\CameraEmulator.exe --screen 800x600
build\Release\CameraEmulator.exe --uninstall
```

### Temporary camera

If the camera isn't installed, `CameraEmulator.exe [image.png]` exposes it only until you press
Ctrl+C. To change the image or the layout while it runs:

```bat
build\Release\CameraEmulator.exe --update path\to\other.png
build\Release\CameraEmulator.exe --update --layout fit
```

### Broadcasting a zone of the screen

```bat
build\Release\CameraEmulator.exe --screen
build\Release\CameraEmulator.exe --screen 800x600
build\Release\CameraEmulator.exe --screen 100,200,800,600
```

- **No value:** the screen dims and you drag a rectangle around the zone (Esc or right click cancels).
- **`WIDTHxHEIGHT`:** a zone of that size, centered on the monitor under the mouse.
- **`x,y,width,height`:** an exact zone, relative to the primary monitor's top-left corner.

Sizes are in physical pixels, up to 3840×2160. A red frame with a "drag to move" tab marks the
zone. The frame isn't captured, and the inside of the zone stays clickable. **Drag the frame or
the tab to move the zone**; the capture follows on the next frame, and the zone stays on screen.
To stop the broadcast from anywhere, press **Ctrl+Alt+X** (a global shortcut), or Ctrl+C in the
console. Choose another shortcut with `--stop-key`, for example `--stop-key ctrl+shift+f12`. A
shortcut is any combination of `ctrl`, `alt`, `shift` and `win`, plus one letter, digit or
`F1`–`F24`. If the shortcut is already taken by another application, the app says so and Ctrl+C
still works. When the camera is installed, the app exits after the shortcut. When the app hosts a
temporary camera, it keeps running so the camera doesn't disappear under the emulator: the camera
shows the image again, and Ctrl+C removes it.

When the broadcast stops, the app prints the last position so you can reuse it with `--screen`.
The zone is fitted into the camera frame using the current layout.

To broadcast to a camera that is already running, without restarting it:

```bat
build\Release\CameraEmulator.exe --update --screen
```

The capture runs at 30 fps in `CameraEmulator.exe`, because the Frame Server service runs in
session 0 and can't see your desktop. Frames are passed to the source through the shared memory
section `Global\CameraEmulatorScreen` (see `src/common/ScreenShare.h`). The source creates that
section when a client starts streaming, and the app attaches to it within half a second. When the
broadcast stops (Ctrl+C), the camera goes back to the image within 2 seconds.

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
- **"Can't connect to the camera" right after the AVD starts:** the AVD resumed from a quick-boot
  snapshot that had the camera app open, and that camera session is stale. Close and reopen the
  camera app, or start the AVD with `-no-snapshot-load`.
- **Image cropped or zoomed in the emulator:** check the layout is `android`, and that the AVD
  really uses CameraEmulator. `hw.camera.back=webcamN` in `config.ini` refers to the
  `-webcam-list` order, which changes when other cameras appear.
- **Logs:** the source writes `[CameraEmulator]` lines with `OutputDebugString`. View them in
  Sysinternals DebugView with *Capture Global Win32* enabled, run as administrator. Lines from the
  `CameraEmulator.exe` process also go to `C:\ProgramData\CameraEmulator\source.log`.
- **Test without Frame Server:**
  `build\Release\SourceTest.exe build\Release\CameraEmulatorSource.dll frame.bmp [width height [seconds]]`.
  Outside the service, the source falls back to a `Local\` section, so you can also test the
  screen broadcast. Start `CameraEmulator.exe --update --screen x,y,w,h`, then run `SourceTest`
  with a duration of a few seconds.
