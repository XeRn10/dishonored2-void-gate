@echo off
rem Builds the native setup: native_d2\setup\VoidGateSetup.exe (needs gen\ from tools\vg_payload.py, which
rem tools\vg_build.py runs first). "test" also builds test_cli.exe, the window-less test driver.
set ZIG="%~dp0..\..\toolchain\zig\zig.exe"
set D=%~dp0
%ZIG% cc -target x86_64-windows-gnu -O2 -s -Wall -Wno-unused-function -municode -Wl,--subsystem,windows ^
  -o "%D%VoidGateSetup.exe" "%D%ui.c" "%D%core.c" "%D%inflate.c" "%D%gen\payload_table.c" "%D%setup.rc" ^
  -lbcrypt -ldwmapi -lole32 -luuid -lwindowscodecs -lshell32 -lgdi32 -luser32 -ladvapi32 || exit /b 1
if /i not "%1"=="test" exit /b 0
%ZIG% cc -target x86_64-windows-gnu -O2 -Wall -Wno-unused-function -municode -o "%D%test_cli.exe" "%D%test_cli.c" ^
  "%D%core.c" "%D%inflate.c" "%D%gen\payload_table.c" -lbcrypt || exit /b 1
