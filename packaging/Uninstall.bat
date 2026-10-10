@echo off
rem CameraEmulator uninstaller (installed in "%ProgramFiles%\CameraEmulator" by Install.bat).
rem
rem   1. As the current user: stops the tray app, removes the camera, the startup entry and the
rem      Start menu shortcut.
rem   2. As administrator (one UAC prompt): unregisters the camera component and deletes the
rem      program folder.

setlocal
set "TARGET=%~dp0"
if "%TARGET:~-1%"=="\" set "TARGET=%TARGET:~0,-1%"
set "SHORTCUT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\CameraEmulator.lnk"
set "DATA=%ProgramData%\CameraEmulator"

if /i "%~1"=="/elevated" goto :elevated

echo.
echo  CameraEmulator - uninstallation
echo  ===============================
echo.
echo  This removes the CameraEmulator camera and deletes "%TARGET%".
echo  Windows will ask for administrator rights.
echo.
choice /c YN /m " Continue"
if errorlevel 2 exit /b 1

rem Step 1, as the current user.
taskkill /IM CameraEmulatorTray.exe /F >nul 2>&1
taskkill /IM CameraEmulator.exe /F >nul 2>&1
if exist "%TARGET%\CameraEmulator.exe" "%TARGET%\CameraEmulator.exe" --uninstall
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v CameraEmulator /f >nul 2>&1
if exist "%SHORTCUT%" del "%SHORTCUT%"

set "KEEPDATA=1"
echo.
choice /c YN /m " Also delete your image and settings (%DATA%)"
if not errorlevel 2 set "KEEPDATA=0"

rem Step 2, as administrator: this same script with /elevated. It deletes this folder, so run it
rem from a copy in %TEMP%.
copy /y "%~f0" "%TEMP%\CameraEmulator-Uninstall.bat" >nul
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "try { $p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c \"\"%TEMP%\CameraEmulator-Uninstall.bat\" /elevated \"%TARGET%\" %KEEPDATA%\"' -Verb RunAs -Wait -PassThru; exit $p.ExitCode } catch { exit 1223 }"
if "%ERRORLEVEL%"=="1223" (
    echo.
    echo  The camera was removed for your account, but the program files are still installed:
    echo  administrator rights are required to remove them.
    pause
    exit /b 1
)

echo.
echo  CameraEmulator has been uninstalled.
echo.
pause
exit /b 0


:elevated
set "TARGET=%~2"
net stop FrameServerMonitor >nul 2>&1
echo Y| net stop FrameServer >nul 2>&1
if exist "%TARGET%\CameraEmulatorSource.dll" regsvr32 /u /s "%TARGET%\CameraEmulatorSource.dll"
if exist "%TARGET%" rd /s /q "%TARGET%"
if "%~3"=="0" if exist "%ProgramData%\CameraEmulator" rd /s /q "%ProgramData%\CameraEmulator"
exit /b 0
