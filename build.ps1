param([switch]$OverlayTest, [switch]$KeepBuild, [switch]$MinGW,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RendererName = 'overlay_python')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildDirectory = Join-Path $projectRoot '.build'
if ((Test-Path -LiteralPath $buildDirectory) -and
    ((Get-Item -LiteralPath $buildDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Refusing to use a build directory that is a symbolic link or junction'
}

# Locates a MinGW-w64 g++ able to build both the DLL and the D3D11 renderer. Used with
# -MinGW, or automatically when the MSVC/CMake path is unusable (see below).
function Find-MinGW {
    $candidates = @()
    # $env:MSYS2_ROOT may be unset, so only join it when present.
    if ($env:MSYS2_ROOT) { $candidates += (Join-Path $env:MSYS2_ROOT 'ucrt64\bin\g++.exe') }
    $candidates += 'C:\msys64\ucrt64\bin\g++.exe'
    $candidates += 'C:\msys64\mingw64\bin\g++.exe'
    $fromPath = Get-Command g++ -ErrorAction SilentlyContinue
    if ($fromPath) { $candidates += $fromPath.Source }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            # Require the Windows SDK headers this project actually depends on.
            $include = Join-Path (Split-Path (Split-Path $candidate)) 'include'
            if ((Test-Path (Join-Path $include 'd3d11.h')) -and (Test-Path (Join-Path $include 'dwmapi.h'))) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }
    return $null
}

# True when the MSVC C++ compiler is actually on disk. vswhere reports the VC.Tools
# component even when its payload is missing, so -find (which walks the filesystem) is
# used instead of -requires.
function Test-MsvcTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $false }
    $compilers = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe' 2>$null
    foreach ($compiler in @($compilers)) {
        if ($compiler -and (Test-Path -LiteralPath $compiler)) { return $true }
    }
    return $false
}

function Invoke-MinGWBuild {
    param([string]$Gxx, [bool]$RunTests)
    $compiler = Split-Path $Gxx
    $bin = Join-Path $projectRoot 'bin'
    New-Item -ItemType Directory -Force -Path $bin | Out-Null

    $common = @('-std=c++17', '-O2', '-I', 'src', '-I', 'vendor/imgui',
        '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
        '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
        '-static', '-static-libgcc', '-static-libstdc++')
    $userLibs = @('-luser32', '-lgdi32')
    $renderLibs = @('-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldwmapi') + $userLibs

    # dwm_draw.dll: the Python ctypes surface.
    & $Gxx @common -shared -o (Join-Path $bin 'dwm_draw.dll') `
        src/python_api.cpp src/CDwmMsg.cpp src/DwmScrollPanel.cpp src/DwmHotkeys.cpp @userLibs
    if ($LASTEXITCODE -ne 0) { throw 'Building dwm_draw.dll failed' }

    # The overlay renderer (ImGui + Direct3D 11 + the runtime font library).
    & $Gxx @common -o (Join-Path $bin "$RendererName.exe") `
        src/overlay_demo.cpp src/CDwmMsg.cpp src/CIMGuiDraw.cpp src/MyImGui.cpp src/save_state.cpp `
        src/DwmPrimitives.cpp src/DwmFontLibrary.cpp `
        vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_widgets.cpp `
        vendor/imgui/imgui_impl_dx11.cpp @renderLibs
    if ($LASTEXITCODE -ne 0) { throw 'Building the overlay renderer failed' }

    # C++ example programs.
    & $Gxx @common -o (Join-Path $bin 'draw_example.exe') `
        examples/draw_functions.cpp src/CDwmMsg.cpp @userLibs
    if ($LASTEXITCODE -ne 0) { throw 'Building draw_example failed' }
    & $Gxx @common -o (Join-Path $bin 'scroll_example.exe') `
        examples/scroll_panel.cpp src/CDwmMsg.cpp src/DwmScrollPanel.cpp src/DwmHotkeys.cpp @userLibs
    if ($LASTEXITCODE -ne 0) { throw 'Building scroll_example failed' }

    if ($RunTests) {
        $testDirectory = Join-Path $projectRoot '.build'
        New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
        & $Gxx @common -o (Join-Path $testDirectory 'drawing_api_test.exe') `
            tests/drawing_api.cpp src/CDwmMsg.cpp src/DwmHotkeys.cpp @userLibs
        if ($LASTEXITCODE -ne 0) { throw 'Building drawing_api_test failed' }
        & $Gxx @common -o (Join-Path $testDirectory 'scroll_panel_test.exe') `
            tests/scroll_panel.cpp src/CDwmMsg.cpp src/DwmScrollPanel.cpp src/DwmHotkeys.cpp @userLibs
        if ($LASTEXITCODE -ne 0) { throw 'Building scroll_panel_test failed' }
        # The font library test needs the ImGui core (atlas + widgets).
        & $Gxx @common -o (Join-Path $testDirectory 'font_library_test.exe') `
            tests/font_library.cpp src/DwmFontLibrary.cpp `
            vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_widgets.cpp @userLibs
        if ($LASTEXITCODE -ne 0) { throw 'Building font_library_test failed' }
        & (Join-Path $testDirectory 'drawing_api_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Drawing API test failed' }
        & (Join-Path $testDirectory 'scroll_panel_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Scroll panel test failed' }
        & (Join-Path $testDirectory 'font_library_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Font library test failed' }
    }
}

# Attempts the MSVC/CMake build. Returns $true on success, $false when the toolchain is
# unusable so the caller can fall back to MinGW. MSBuild can fail here for reasons outside
# this project's control: notably a sandboxed or restricted environment where MSBuild's
# file tracker cannot spawn (CL.exe exits with "TRK0002: failed to execute command").
function Try-MsvcBuild {
    param([string]$Renderer, [bool]$RunOverlayTest)
    if (-not (Test-MsvcTools)) { return $false }
    Remove-Item -Recurse -Force $buildDirectory -ErrorAction SilentlyContinue
    & cmake -S . -B .build -G 'Visual Studio 16 2019' -A x64 -T v142 `
        '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release' `
        "-DDWM_OVERLAY_OUTPUT_NAME=$Renderer" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return $false }
    & cmake --build .build --config Release --parallel 1 -- /nr:false /v:minimal 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return $false }
    & ctest --test-dir .build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { return $false }
    if ($RunOverlayTest) {
        & (Join-Path $projectRoot "bin\$Renderer.exe") --self-test
        if ($LASTEXITCODE -ne 0) { throw 'Overlay capture test failed; requires an interactive desktop' }
    }
    return $true
}

Push-Location $projectRoot
try {
    $built = $false
    if (-not $MinGW) {
        Write-Output 'Building with MSVC (Visual Studio 2019 / CMake)...'
        $built = Try-MsvcBuild -Renderer $RendererName -RunOverlayTest ([bool]$OverlayTest)
        if (-not $built) {
            Write-Output 'MSVC/CMake build unavailable (missing toolset, or MSBuild cannot run here); falling back to MinGW-w64.'
        }
    }
    if (-not $built) {
        $gxx = Find-MinGW
        if (-not $gxx) {
            throw ('No usable C++ toolchain found. Install the "Desktop development with C++" workload ' +
                   'for Visual Studio 2019, or install MSYS2 (pacman -S mingw-w64-ucrt-x86_64-gcc), then re-run. ' +
                   'Use -MinGW to force the MinGW-w64 path.')
        }
        Write-Output "Building with MinGW-w64: $gxx"
        Invoke-MinGWBuild -Gxx $gxx -RunTests (-not $OverlayTest)
        if ($OverlayTest) {
            & (Join-Path $projectRoot "bin\$RendererName.exe") --self-test
            if ($LASTEXITCODE -ne 0) { throw 'Overlay capture test failed; requires an interactive desktop' }
        }
    }
    if (-not $KeepBuild) {
        if (Test-Path -LiteralPath $buildDirectory) {
            $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
            if ($resolvedBuild -ne (Join-Path $projectRoot '.build')) { throw 'Unexpected build cleanup path' }
            Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
        }
    }
    Write-Output "Ready: bin\$RendererName.exe, bin\dwm_draw.dll, and Python examples in python\"
} finally { Pop-Location }
