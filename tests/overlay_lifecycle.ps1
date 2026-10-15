param([string]$Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'bin\overlay_python.exe'))
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$projectRoot = Split-Path -Parent $PSScriptRoot
$logDirectory = Join-Path ([IO.Path]::GetTempPath()) "DwmOverlay-Lifecycle-$PID"
$null = New-Item -ItemType Directory -Path $logDirectory -Force
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OverlayProbe {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool GetWindowDisplayAffinity(IntPtr hwnd, out uint affinity);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr key, IntPtr data);
    // Start-Process with redirected output leaves Process.ExitCode unreadable after the
    // process exits, so the exit code is read from a handle opened before the wait.
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr handle);
    public const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
    public const uint SYNCHRONIZE = 0x00100000;
    public static IntPtr Find(uint wanted) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, data) => { uint pid; GetWindowThreadProcessId(hwnd, out pid); if (pid == wanted && IsWindowVisible(hwnd)) { found = hwnd; return false; } return true; }, IntPtr.Zero);
        return found;
    }
}
'@
foreach ($mode in @('demo','listen')) {
    $startArgs = @{ FilePath = $Executable; WorkingDirectory = $projectRoot; PassThru = $true; WindowStyle = 'Hidden'; RedirectStandardOutput = (Join-Path $logDirectory "$mode.stdout.log"); RedirectStandardError = (Join-Path $logDirectory "$mode.stderr.log") }
    if ($mode -eq 'listen') { $startArgs.ArgumentList = '--listen' }
    $overlayProcess = Start-Process @startArgs
    # Open our own handle to the process: Process.Handle is $null here, and Process.ExitCode
    # is unreadable after exit when Start-Process redirected the output streams.
    $processHandle = [OverlayProbe]::OpenProcess(
        [OverlayProbe]::PROCESS_QUERY_LIMITED_INFORMATION -bor [OverlayProbe]::SYNCHRONIZE, $false,
        [uint32]$overlayProcess.Id)
    if ($processHandle -eq [IntPtr]::Zero) { throw 'Cannot open the overlay process handle' }
    try {
        $window = [IntPtr]::Zero
        for ($attempt = 0; $attempt -lt 50 -and $window -eq [IntPtr]::Zero; $attempt++) {
            Start-Sleep -Milliseconds 100
            if ($overlayProcess.HasExited) { throw "Overlay startup failed: $(Get-Content -LiteralPath $startArgs.RedirectStandardError -Raw)" }
            $window = [OverlayProbe]::Find([uint32]$overlayProcess.Id)
        }
        if ($window -eq [IntPtr]::Zero) { throw 'No visible overlay window found' }
        [uint32]$affinity = 0
        if (-not [OverlayProbe]::GetWindowDisplayAffinity($window, [ref]$affinity) -or $affinity -ne 17) { throw 'Startup affinity mismatch' }
        [void][OverlayProbe]::PostMessageW($window, 0x0312, [IntPtr]2, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        if ([OverlayProbe]::IsWindowVisible($window)) { throw 'Hide command failed' }
        [void][OverlayProbe]::PostMessageW($window, 0x0312, [IntPtr]2, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        if (-not [OverlayProbe]::IsWindowVisible($window)) { throw 'Show command failed' }
        if (-not [OverlayProbe]::GetWindowDisplayAffinity($window, [ref]$affinity) -or $affinity -ne 17) { throw 'Show affinity mismatch' }
        [void][OverlayProbe]::PostMessageW($window, 0x0312, [IntPtr]1, [IntPtr]::Zero)
        if (-not $overlayProcess.WaitForExit(5000)) { throw 'Quit command timed out' }
        # Process.ExitCode is unreadable here because Start-Process redirected the output;
        # read the code from the native handle retained before the wait instead.
        $exitCode = [uint32]0
        if (-not [OverlayProbe]::GetExitCodeProcess($processHandle, [ref]$exitCode)) {
            throw "Cannot read exit code: $([ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error()).Message)"
        }
        if ($exitCode -ne 0 -or [OverlayProbe]::IsWindow($window)) {
            throw "Exit/cleanup failed: exit=$exitCode, window=$([OverlayProbe]::IsWindow($window))"
        }
        Write-Output "PASS $mode`: startup, affinity 0x11, hide/show keeps affinity, clean quit."
    } finally {
        if ($processHandle -ne [IntPtr]::Zero) { [void][OverlayProbe]::CloseHandle($processHandle) }
        if (-not $overlayProcess.HasExited) { Stop-Process -Id $overlayProcess.Id }
        $overlayProcess.Dispose()
    }
}
