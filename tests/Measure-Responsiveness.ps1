#Requires -RunAsAdministrator
param(
    [ValidateSet('ARM64','x64','Win32')][string]$Platform = 'ARM64',
    [switch]$FullScan,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$folder = if ($Platform -eq 'Win32') { 'Release' } else { "$Platform\Release" }
$exe = Join-Path $root "$folder\Find My File!.exe"
$output = Join-Path $root "build\responsiveness-$Platform.json"
$session = (Get-Process -Id $PID).SessionId
if (Get-Process | Where-Object { $_.ProcessName -eq 'Find My File!' -and $_.SessionId -eq $session }) {
    throw 'Close the existing FindMyFile window before measuring startup.'
}
Add-Type @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class ResponsivenessUi {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")]
 static extern IntPtr SendText(IntPtr window,uint message,IntPtr wp,string text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")]
 static extern IntPtr ReadText(IntPtr window,uint message,IntPtr wp,StringBuilder text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW")]
 static extern IntPtr Send(IntPtr window,uint message,IntPtr wp,IntPtr lp,uint flags,uint timeout,out IntPtr result);
 public static double Message(IntPtr window,uint message,int wp) {
   IntPtr result; var watch=Stopwatch.StartNew();
   if(Send(window,message,(IntPtr)wp,IntPtr.Zero,2,5000,out result)==IntPtr.Zero) throw new Exception("UI message timed out");
   return watch.Elapsed.TotalMilliseconds;
 }
 public static double Query(IntPtr window,string text) {
   IntPtr result; var watch=Stopwatch.StartNew();
   if(SendText(window,12,IntPtr.Zero,text,2,5000,out result)==IntPtr.Zero) throw new Exception("Typing timed out");
   return watch.Elapsed.TotalMilliseconds;
 }
 public static string Text(IntPtr window) {
   IntPtr result; var text=new StringBuilder(2048);
   if(ReadText(window,13,(IntPtr)text.Capacity,text,2,5000,out result)==IntPtr.Zero) throw new Exception("Reading status timed out");
   return text.ToString();
 }
}
'@
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
$process = $null
$beats = [Collections.Generic.List[double]]::new()
$keys = [Collections.Generic.List[double]]::new()
$result = @{platform=$Platform;passed=$false;cache_reset=$false}
function Wait-IndexReady([IntPtr]$window) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (![ResponsivenessUi]::IsWindowEnabled([ResponsivenessUi]::GetDlgItem($window,3001))) {
        if ($process.HasExited) { throw 'Application exited during index initialization.' }
        $beats.Add([ResponsivenessUi]::Message($window,0,0))
        if ((Get-Date) -ge $deadline) { throw 'Index initialization exceeded the test deadline.' }
        Start-Sleep -Milliseconds 100
    }
}
try {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -PassThru
    $window = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds(30)
    while ($window -eq [IntPtr]::Zero) {
        if ($process.HasExited -or (Get-Date) -ge $deadline) { throw 'Application window did not appear.' }
        $process.Refresh(); $window=$process.MainWindowHandle
        Start-Sleep -Milliseconds 50
    }
    $result.window_visible_ms = $watch.ElapsedMilliseconds
    Start-Sleep -Milliseconds 250
    $result.maximized_during_startup = [ResponsivenessUi]::IsZoomed($window)
    Wait-IndexReady $window
    $result.startup_ready_ms = $watch.ElapsedMilliseconds
    if ($FullScan) {
        $drive = [ResponsivenessUi]::GetDlgItem($window,2000)
        if ([ResponsivenessUi]::Text($drive) -ne 'Scan C:') { throw 'The first drive button is not C:; no other drive was scanned.' }
        $watch.Restart()
        $beats.Add([ResponsivenessUi]::Message($window,0x111,2000))
        Wait-IndexReady $window
        $result.full_c_scan_ms = $watch.ElapsedMilliseconds
    }
    $edit = [ResponsivenessUi]::GetDlgItem($window,3000)
    $status = [ResponsivenessUi]::GetDlgItem($window,1003)
    $warp = [ResponsivenessUi]::GetDlgItem($window,3007)
    foreach ($text in @('e','ev','eva','eval','evalu','eval')) {
        $keys.Add([ResponsivenessUi]::Query($edit,$text))
        Start-Sleep -Milliseconds 400
        $beats.Add([ResponsivenessUi]::Message($window,0,0))
    }
    $deadline = (Get-Date).AddSeconds(60)
    while ([ResponsivenessUi]::Text($status) -notlike 'Found *') {
        if ($process.HasExited -or (Get-Date) -ge $deadline) { throw 'The latest filename search did not finish.' }
        $beats.Add([ResponsivenessUi]::Message($window,0,0))
        Start-Sleep -Milliseconds 50
    }
    $result.filename_status = [ResponsivenessUi]::Text($status)
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $warpText=[ResponsivenessUi]::Text($warp)
        if ($warpText -like 'WARP*' -and $warpText -notlike '*checking*') { break }
        if ((Get-Date) -ge $deadline) { throw 'WARP response did not complete.' }
        $beats.Add([ResponsivenessUi]::Message($window,0,0))
        Start-Sleep -Milliseconds 50
    } while ($true)
    $result.warp_status = $warpText
    $sorted = @($keys | Sort-Object)
    $result.input_p95_ms = [math]::Round($sorted[[math]::Floor(($sorted.Count-1)*.95)],2)
    $result.input_max_ms = [math]::Round(($keys | Measure-Object -Maximum).Maximum,2)
    $result.heartbeat_max_ms = [math]::Round(($beats | Measure-Object -Maximum).Maximum,2)
    $process.Refresh()
    $result.private_mb = [math]::Round($process.PrivateMemorySize64/1MB)
    if (!$result.maximized_during_startup -or $result.input_max_ms -ge 500 -or $result.heartbeat_max_ms -ge 1000) {
        throw 'UI responsiveness did not meet the measured acceptance limits.'
    }
    if ($warpText -match 'timed out|not accepted|unavailable') { throw 'WARP did not return a usable response.' }
    $null = $process.CloseMainWindow()
    if (!$process.WaitForExit(90000) -or $process.ExitCode -ne 0) { throw 'Application did not close cleanly.' }
    $result.passed = $true
} catch {
    $result.error = $_.Exception.Message
} finally {
    if ($process -and !$process.HasExited) {
        $null = $process.CloseMainWindow()
        if (!$process.WaitForExit(30000)) { Stop-Process -Id $process.Id }
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $output -Encoding UTF8
}
Get-Content -LiteralPath $output
if (!$result.passed) { exit 1 }
