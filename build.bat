@echo off
setlocal
cd /d "%~dp0"
if exist C:\msys64\ucrt64\bin\g++.exe set "PATH=C:\msys64\ucrt64\bin;%PATH%"

where cl >nul 2>&1
if %errorlevel%==0 goto :msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :trygcc
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS goto :trygcc
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>&1
if %errorlevel%==0 goto :msvc

:trygcc
where g++ >nul 2>&1
if %errorlevel%==0 goto :gcc

echo No C++ compiler found.
echo Install MSYS2 ucrt64 g++ or Visual Studio C++ tools, then run this again.
if /I not "%~1"=="nopause" pause
exit /b 1

:msvc
cl /nologo /utf-8 /std:c++17 /O2 /W3 /EHsc /DUNICODE /D_UNICODE /Fe:HuaBi.exe src\main.cpp /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup gdiplus.lib user32.lib gdi32.lib shell32.lib ole32.lib shlwapi.lib
set ERR=%errorlevel%
del /q main.obj >nul 2>&1
goto :done

:gcc
g++ -std=c++17 -O2 -Wall -municode -mwindows -static -finput-charset=UTF-8 -fexec-charset=UTF-8 -o HuaBi.exe src\main.cpp -lgdiplus -lole32 -lshell32 -lshlwapi -luuid -lgdi32 -luser32
set ERR=%errorlevel%
goto :done

:done
if not "%ERR%"=="0" (
  echo Build failed.
  if /I not "%~1"=="nopause" pause
  exit /b %ERR%
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_shortcut.ps1"
echo.
echo Built HuaBi.exe V1.0
echo Double-click the shortcut next to it. Press Pause to draw, Esc to finish.
if /I not "%~1"=="nopause" pause
exit /b 0
