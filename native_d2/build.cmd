@echo off
rem Builds the Void Gate mod for Dishonored 2 into native_d2\:
rem   dinput8.dll  - x64 DirectInput proxy + stable hooks (host.c); needs a game restart to update
rem   voidgate.dll - gameplay module (vg.c); hot-reloaded by the host when the copy in the game folder changes
set ZIG="%~dp0..\toolchain\zig\zig.exe"
set FLAGS=-target x86_64-windows-gnu -O2 -s -shared -Wall -Wno-unused-function
if /i "%1"=="module" goto module
if /i "%1"=="test" goto test
%ZIG% cc %FLAGS% -o "%~dp0dinput8.dll" "%~dp0proxy.c" "%~dp0host.c" "%~dp0dinput8.def" -luser32 || exit /b 1
:module
%ZIG% cc %FLAGS% -o "%~dp0voidgate.dll" "%~dp0vg.c" -luser32 || exit /b 1
exit /b 0
:test
rem offline tests (no game): portal math + trigger Monte Carlo
%ZIG% cc -target x86_64-windows-gnu -O2 -Wall -o "%~dp0tests\test_portal.exe" "%~dp0tests\test_portal.c" || exit /b 1
"%~dp0tests\test_portal.exe" %2 %3
