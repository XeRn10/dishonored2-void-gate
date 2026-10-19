# Dishonored 2 dev loop helper: (re)start the game and bring its window to the front.
#   powershell -File d2run.ps1 [-Kill] [-Deploy]
#   -Kill    stop a running Dishonored2.exe by PID first (no save!)
#   -Deploy  rebuild native_d2 and copy dinput8.dll + voidgate.dll into the game folder before starting
#   -Module  rebuild only voidgate.dll and swap it into the running game (hot reload; nothing else happens)
param([switch]$Kill, [switch]$Deploy, [switch]$NoStart, [switch]$Module)
$game = "D:\Dishonored 2"
if ($Module) {
    & "D:\DishonoredMod\native_d2\build.cmd" module
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    # copy under a temp name, then rename: the host never sees a half-written file
    Copy-Item "D:\DishonoredMod\native_d2\voidgate.dll" "$game\voidgate.dll.tmp" -Force
    Move-Item "$game\voidgate.dll.tmp" "$game\voidgate.dll" -Force
    "module deployed"
    return
}
if ($Kill) {
    $p = Get-Process Dishonored2 -ErrorAction SilentlyContinue
    if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 3; "stopped PID $($p.Id)" }
}
if ($Deploy) {
    & "D:\DishonoredMod\native_d2\build.cmd"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    Copy-Item "D:\DishonoredMod\native_d2\dinput8.dll" "$game\dinput8.dll" -Force
    Copy-Item "D:\DishonoredMod\native_d2\voidgate.dll" "$game\voidgate.dll" -Force
    "deployed dinput8.dll + voidgate.dll"
}
if ($NoStart) { return }
if (-not (Get-Process Dishonored2 -ErrorAction SilentlyContinue)) {
    Remove-Item "$game\dmod_d2.log" -ErrorAction SilentlyContinue
    Start-Process "$game\Dishonored2.exe" -WorkingDirectory $game
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Seconds 1
        $p = Get-Process Dishonored2 -ErrorAction SilentlyContinue
        if ($p -and $p.MainWindowHandle -ne 0) { break }
    }
}
Add-Type @"
using System; using System.Runtime.InteropServices;
public class D2Win {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, int f, IntPtr e);
}
"@
$p = Get-Process Dishonored2
[D2Win]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [D2Win]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
[D2Win]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
"foreground: $([D2Win]::SetForegroundWindow($p.MainWindowHandle)) PID $($p.Id)"
