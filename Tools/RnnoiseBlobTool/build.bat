@echo off
rem RnnoiseBlobTool: builds the tool with MSVC and writes the RNNoise model blobs to
rem ThirdParty\rnnoise\models\ (rnnoise_standard.bin, rnnoise_light.bin). Run by hand; not part of
rem the plugin build. Needs the upstream sources (78 / 30 MB model files) at
rem   %RNNOISE_SRC% (environment variable; default: <repo>\..\_upstream\rnnoise\src, i.e. an upstream
rem   checkout next to the repository folder; commit 70f1d25, model 0a8755f8 extracted into src\).
rem Visual Studio is located with vswhere.exe. "build.bat --print-vcvars" only prints the
rem vcvars64.bat it would use and exits.
setlocal
set "HERE=%~dp0"

rem Find the newest Visual Studio with the x64 C++ tools (vcvars64.bat also needs vswhere.exe on PATH)
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" (echo vswhere.exe not found in "%VSWHERE_DIR%" - install Visual Studio 2017 or later with the C++ workload & exit /b 1)
set "PATH=%VSWHERE_DIR%;%PATH%"
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (echo No Visual Studio installation with the x64 C++ tools found by vswhere.exe & exit /b 1)
set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo vcvars64.bat not found: "%VCVARS%" & exit /b 1)
if /i "%~1"=="--print-vcvars" (echo "%VCVARS%"& exit /b 0)

if not defined RNNOISE_SRC set "RNNOISE_SRC=%HERE%..\..\..\_upstream\rnnoise\src"
if not exist "%RNNOISE_SRC%\rnnoise_data.c" (echo RNNoise sources not found: "%RNNOISE_SRC%" - set RNNOISE_SRC to an upstream rnnoise\src folder & exit /b 1)
set "OUT=%HERE%..\..\ThirdParty\rnnoise\models"
set "OBJ=%TEMP%\nsp-rnnoise-blob-tool"
if not exist "%OBJ%" mkdir "%OBJ%"

call "%VCVARS%" >nul || exit /b 1
if not defined VCToolsInstallDir (echo vcvars64.bat failed & exit /b 1)

cl /nologo /O1 /c /I"%RNNOISE_SRC%" /Fo"%OBJ%\\" "%HERE%RnnoiseBlobTool.c" "%HERE%BlobStandard.c" "%HERE%BlobLight.c" || exit /b 1
"%VCToolsInstallDir%bin\Hostx64\x64\link.exe" /nologo /OUT:"%OBJ%\RnnoiseBlobTool.exe" "%OBJ%\RnnoiseBlobTool.obj" "%OBJ%\BlobStandard.obj" "%OBJ%\BlobLight.obj" || exit /b 1
rem full path: cmd may not search the current directory (NoDefaultCurrentDirectoryInExePath)
"%OBJ%\RnnoiseBlobTool.exe" "%OUT%" || exit /b 1
