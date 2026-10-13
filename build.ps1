param([switch]$OverlayTest, [switch]$KeepBuild,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RendererName = 'overlay_python')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildDirectory = Join-Path $projectRoot '.build'
if ((Test-Path -LiteralPath $buildDirectory) -and
    ((Get-Item -LiteralPath $buildDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Refusing to use a build directory that is a symbolic link or junction'
}
Push-Location $projectRoot
try {
    & cmake -S . -B .build -G 'Visual Studio 16 2019' -A x64 -T v142 `
        '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release' `
        "-DDWM_OVERLAY_OUTPUT_NAME=$RendererName"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & cmake --build .build --config Release --parallel 1 -- /nr:false /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & ctest --test-dir .build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Drawing API test failed' }
    if ($OverlayTest) {
        & (Join-Path $projectRoot "bin\$RendererName.exe") --self-test
        if ($LASTEXITCODE -ne 0) { throw 'Overlay capture test failed; requires an interactive desktop' }
    }
    if (-not $KeepBuild) {
        $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
        if ($resolvedBuild -ne (Join-Path $projectRoot '.build')) { throw 'Unexpected build cleanup path' }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
    Write-Output "Ready: bin\$RendererName.exe, bin\dwm_draw.dll, and Python examples in python\"
} finally { Pop-Location }
