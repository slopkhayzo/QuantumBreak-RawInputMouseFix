@echo off
rem Builds build\Release\QBMouseFix.asi with the MSVC x64 toolchain and the CMake bundled with Visual Studio.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio 2022 or newer with "Desktop development with C++".
    exit /b 1
)
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio installation with the x64 C++ tools found.
    exit /b 1
)
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"

"%CMAKE%" -S "%~dp0." -B "%~dp0build" -A x64 || exit /b 1
"%CMAKE%" --build "%~dp0build" --config Release || exit /b 1
echo Built %~dp0build\Release\QBMouseFix.asi
