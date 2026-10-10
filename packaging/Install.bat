@echo off
rem CameraEmulator installer.
rem
rem   1. As administrator (one UAC prompt): copies the files to "%ProgramFiles%\CameraEmulator" and
rem      registers the camera component (CameraEmulatorSource.dll).
rem   2. As the current user: installs the camera permanently, creates a Start menu shortcut and
rem      starts the tray app.
rem
rem The files are copied out of this folder because Windows' camera service cannot read folders
rem inside user profiles (Downloads, Desktop...). Running Install.bat again upgrades the program.

setlocal
set "SOURCE=%~dp0"
set "TARGET=%ProgramFiles%\CameraEmulator"
set "CLSID={9C5E4B7A-3D21-4F8E-A6B0-1E7C52D9F3A4}"
set "SHORTCUT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\CameraEmulator.lnk"

if /i "%~1"=="/elevated" goto :elevated

echo.
echo  CameraEmulator - installation
echo  =============================
echo.
echo  The program will be installed in "%TARGET%".
echo  Windows will ask for administrator rights to copy the files and register the camera.
echo.
pause

if not exist "%SOURCE%CameraEmulatorSource.dll" (
    echo.
    echo  ERROR: CameraEmulatorSource.dll was not found next to Install.bat.
    echo  Extract the whole zip file first, then run Install.bat from the extracted folder.
    goto :failed
)

rem Step 1, as administrator: this same script with /elevated.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c \"\"%~f0\" /elevated\"' -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { exit 1223 }"
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="1223" (
    echo.
    echo  Installation cancelled: administrator rights are required.
    goto :failed
)
if not "%RESULT%"=="0" (
    echo.
    echo  ERROR: copying or registering the files failed ^(code %RESULT%^).
    goto :failed
)
reg query "HKLM\SOFTWARE\Classes\CLSID\%CLSID%\InprocServer32" >nul 2>&1
if errorlevel 1 (
    echo.
    echo  ERROR: the camera component is not registered.
    goto :failed
)
echo  Files copied and camera component registered.

rem Step 2, as the current user.
"%TARGET%\CameraEmulator.exe" --install
if errorlevel 1 (
    echo.
    echo  ERROR: the camera could not be installed.
    goto :failed
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = (New-Object -ComObject WScript.Shell).CreateShortcut('%SHORTCUT%'); $s.TargetPath = '%TARGET%\CameraEmulatorTray.exe'; $s.WorkingDirectory = '%TARGET%'; $s.Description = 'CameraEmulator virtual webcam'; $s.Save()"
echo  Start menu shortcut created.

start "" "%TARGET%\CameraEmulatorTray.exe"

echo.
echo  Installation complete.
echo.
echo  - The CameraEmulator icon is in the notification area (click the ^^ arrow if it is hidden).
echo  - In the Android Emulator, select the webcam named CameraEmulator as the back camera.
echo  - Help: "%TARGET%\CameraEmulator-Help.html", or Help in the tray menu.
echo  - To uninstall: "%TARGET%\Uninstall.bat".
echo.
pause
exit /b 0

:failed
echo.
pause
exit /b 1


:elevated
rem Upgrade: stop the tray app and release the camera component so the files can be replaced.
taskkill /IM CameraEmulatorTray.exe /F >nul 2>&1
net stop FrameServerMonitor >nul 2>&1
echo Y| net stop FrameServer >nul 2>&1

robocopy "%SOURCE%." "%TARGET%" CameraEmulatorTray.exe CameraEmulator.exe CameraEmulatorSource.dll CameraEmulator-Help.html Uninstall.bat /R:3 /W:1 /NJH /NJS /NFL /NDL /NP >nul
rem robocopy: 0-7 = success, 8 or more = failure.
if errorlevel 8 exit /b 2

regsvr32 /s "%TARGET%\CameraEmulatorSource.dll"
if errorlevel 1 exit /b 3
exit /b 0
