@echo off
setlocal
for %%p in ("%~dp0..") do set "PROJECTROOT=%%~fp"
set "CHECKDIR=%PROJECTROOT%\Saved\Checks\run-%RANDOM%-%RANDOM%"
mkdir "%CHECKDIR%"
if errorlevel 1 exit /b 3
echo Native replay logs: %CHECKDIR%
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if not exist "%VSWHERE%" (
    echo Visual Studio discovery missing.>"%CHECKDIR%\environment.log"
    exit /b 2
)
for /f "usebackq delims=" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%v"
if not defined VSDIR (
    echo Visual Studio C++ tools not found.>"%CHECKDIR%\environment.log"
    exit /b 2
)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64 >"%CHECKDIR%\vs-setup.log" 2>&1
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" goto finish
where cl >"%CHECKDIR%\environment.log" 2>&1
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" goto finish
cl /nologo /EHsc /std:c++17 "%PROJECTROOT%\Tools\ReplayCheck.cpp" /Fo"%CHECKDIR%\ReplayCheck.obj" /Fe"%CHECKDIR%\ReplayCheck.exe" >"%CHECKDIR%\compile.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%CHECKDIR%\compile.log"
if not "%RESULT%"=="0" goto finish
"%CHECKDIR%\ReplayCheck.exe" >"%CHECKDIR%\test.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%CHECKDIR%\test.log"
:finish
>"%CHECKDIR%\exit-code.txt" echo %RESULT%
exit /b %RESULT%
