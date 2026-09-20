param($arch, $tag)

# Windows PowerShell does not turn a non-zero exit code from a native program
# into a terminating error, so without 'Stop' plus the explicit $LASTEXITCODE
# checks below a failed build only prints an error and the step still reports
# success - leaving the job to upload an empty artifact directory.
$ErrorActionPreference = 'Stop'

$root_dir = (Resolve-Path (Join-Path $PSScriptRoot "../../")).Path
Write-Host "Start"
Set-Location $root_dir

function Assert-NativeSuccess {
    param([string]$Step)
    if ($LASTEXITCODE -ne 0) {
        throw "$Step exited with code $LASTEXITCODE"
    }
}

try
{
    # cmake-js owns the Windows Node-API import library and delay-load hook.
    # Building through it avoids accidentally linking the legacy NW.js
    # node64.lib (which imports node.dll directly and cannot load in Electron).
    $cmakeArch = "x64"
    if ($arch -eq "ia32" -or $arch -eq "x86") {
        $cmakeArch = "ia32"
    }

    # Ninja is requested explicitly.  cmake-js 7.3.x vendors a copy of the
    # node-gyp Visual Studio finder that only knows VS 2017/2019/2022, so on a
    # runner that ships Visual Studio 2026 (major version 18) it aborts before
    # configuring with 'unknown version "undefined"' / 'could not find a
    # version of Visual Studio 2017 or newer to use'.  Naming the generator
    # skips that detection completely and builds with the cl.exe that
    # setup-msvc-dev already put on PATH, which works for any VS version.
    pnpm exec cmake-js compile --runtime electron --runtime-version 36.6.0 --arch $cmakeArch --config Release --generator Ninja
    Assert-NativeSuccess "cmake-js compile"

    node "$root_dir/test/api.js"
    Assert-NativeSuccess "test/api.js"

    $outDir = Join-Path $root_dir "tmp/build"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $modules = @(Get-ChildItem -Path (Join-Path $root_dir "build") -Filter "*.node" -Recurse -ErrorAction SilentlyContinue)
    if ($modules.Count -eq 0) {
        throw "No *.node build output found under $root_dir/build"
    }
    foreach ($module in $modules) {
        $target = Join-Path $outDir "skyline-$($module.BaseName)-win32-$arch-$tag.node"
        Write-Host "$($module.FullName) -> $target"
        Move-Item -LiteralPath $module.FullName -Destination $target -Force
    }
}
catch
{
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
}
