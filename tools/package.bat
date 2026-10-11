@echo off
rem Builds CameraEmulator (Release), signs the binaries and packages the files end users need into
rem dist\CameraEmulator-<version>-<commit>.zip:
rem
rem   CameraEmulatorTray.exe, CameraEmulator.exe, CameraEmulatorSource.dll,
rem   CameraEmulator-Help.html, Install.bat, Uninstall.bat, ReadMe.txt
rem
rem Usage: tools\package.bat [--no-sign]   (from any folder)
rem
rem Signing uses signtool.exe from the Windows SDK, with SHA-256 and an RFC 3161 timestamp.
rem Optional settings, from the environment or tools\sign-config.bat (not committed, see
rem tools\sign-config.example.bat):
rem   SIGN_CERT_SHA1      thumbprint of the certificate in the certificate store
rem                       (default: signtool picks the best code signing certificate)
rem   SIGN_TIMESTAMP_URL  timestamp server (default: http://time.certum.pl)

setlocal
cd /d "%~dp0.."

set "SIGN=1"
if /i "%~1"=="--no-sign" set "SIGN=0"
if not "%~1"=="" if /i not "%~1"=="--no-sign" goto :usage

rem CMake: from PATH, or the copy bundled with Visual Studio.
set "CMAKE=cmake"
where cmake >nul 2>&1 && goto :have_cmake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem "call": keeps cmd from stripping the quotes of a path containing parentheses.
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CMAKE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
:have_cmake

rem Separate build tree: never blocked by binaries running from the development build.
set "BUILD=build-package"
echo Building Release in %BUILD%...
"%CMAKE%" -S . -B %BUILD% >nul || goto :build_failed
"%CMAKE%" --build %BUILD% --config Release --target CameraEmulatorSource CameraEmulator CameraEmulatorTray || goto :build_failed

set "OUT=%BUILD%\Release"
set FILES="%OUT%\CameraEmulatorTray.exe" "%OUT%\CameraEmulator.exe" "%OUT%\CameraEmulatorSource.dll"
if "%SIGN%"=="0" (
    echo.
    echo Signing skipped ^(--no-sign^).
    goto :stage
)
call :sign || goto :sign_failed

:stage
rem Package name: file version (from CMakeLists.txt, written by CMake) and commit.
set "VERSION=0.0.0.0"
if exist "%BUILD%\generated\version.txt" set /p VERSION=<"%BUILD%\generated\version.txt"
set "COMMIT=local"
for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set "COMMIT=%%i"
git diff --quiet HEAD -- 2>nul || set "COMMIT=%COMMIT%-modified"
set "NAME=CameraEmulator-%VERSION%-%COMMIT%"
if "%SIGN%"=="0" set "NAME=%NAME%-unsigned"

set "STAGE=dist\CameraEmulator"
if exist "%STAGE%" rd /s /q "%STAGE%"
mkdir "%STAGE%" || goto :failed

for %%f in (CameraEmulatorTray.exe CameraEmulator.exe CameraEmulatorSource.dll CameraEmulator-Help.html) do (
    copy /y "%OUT%\%%f" "%STAGE%\" >nul || goto :failed
)
for %%f in (Install.bat Uninstall.bat ReadMe.txt) do (
    copy /y "packaging\%%f" "%STAGE%\" >nul || goto :failed
)

if exist "dist\%NAME%.zip" del "dist\%NAME%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%' -DestinationPath 'dist\%NAME%.zip'" || goto :failed

echo.
echo Package created: dist\%NAME%.zip
for %%f in ("%STAGE%\*") do echo   %%~nxf
exit /b 0


rem Signs and verifies %FILES%. Returns non-zero on failure.
:sign
call :find_signtool || exit /b 1
if exist "%~dp0sign-config.bat" call "%~dp0sign-config.bat"
if not defined SIGN_TIMESTAMP_URL set "SIGN_TIMESTAMP_URL=http://time.certum.pl"
set "CERT=/a"
if defined SIGN_CERT_SHA1 set "CERT=/sha1 %SIGN_CERT_SHA1%"

echo.
echo Signing with %SIGNTOOL%
echo   certificate: %CERT%   timestamp: %SIGN_TIMESTAMP_URL%
"%SIGNTOOL%" sign %CERT% /fd SHA256 /tr "%SIGN_TIMESTAMP_URL%" /td SHA256 /d "CameraEmulator" %FILES% || exit /b 1
"%SIGNTOOL%" verify /pa /q %FILES% || exit /b 1
echo Signatures verified.
exit /b 0

rem Sets SIGNTOOL: from PATH, or the most recent Windows 10/11 SDK.
:find_signtool
set "SIGNTOOL="
for /f "delims=" %%i in ('where signtool 2^>nul') do (
    set "SIGNTOOL=%%i"
    exit /b 0
)
set "KITS=%ProgramFiles(x86)%\Windows Kits\10\bin"
for /f "delims=" %%v in ('dir /b /ad /o-n "%KITS%\10.*" 2^>nul') do if exist "%KITS%\%%v\x64\signtool.exe" (
    set "SIGNTOOL=%KITS%\%%v\x64\signtool.exe"
    exit /b 0
)
echo signtool.exe not found: install the Windows SDK, or add signtool to PATH.
exit /b 1

:usage
echo Usage: %~nx0 [--no-sign]
exit /b 2

:build_failed
echo.
echo Build failed: see the errors above.
exit /b 1

:sign_failed
echo.
echo Signing failed: see the errors above. Check that the code signing certificate is available
echo (for a cloud or hardware token, that its software is running and signed in), or use
echo --no-sign to create an unsigned package.
exit /b 1

:failed
echo.
echo Packaging failed.
exit /b 1
