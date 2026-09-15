param($arch, $tag)
$root_dir = Resolve-Path (Join-Path $PSScriptRoot "../../")
Write-Host "Start"
cd $root_dir
try
{
    # cmake-js owns the Windows Node-API import library and delay-load hook.
    # Building through it avoids accidentally linking the legacy NW.js
    # node64.lib (which imports node.dll directly and cannot load in Electron).
    $cmakeArch = "x64"
    if ($arch -eq "ia32" -or $arch -eq "x86") {
        $cmakeArch = "ia32"
    }
    pnpm exec cmake-js compile --runtime electron --runtime-version 36.6.0 --arch $cmakeArch --config Release
    node "$root_dir/test/api.js"
    mkdir "$root_dir/tmp/build"
    Write-Host "$root_dir/build"
    Get-ChildItem -Path "$root_dir/build/" -Filter "*.node" -Recurse | ForEach-Object {
        try{
            # 对每个文件执行操作，例如输出文件名
            $Name = $_.BaseName
            $FullName = $_.FullName
            Write-Host $FullName
            mv "$FullName" "$root_dir/tmp/build/skyline-$Name-win32-$arch-$tag.node"
        }catch{
            Write-Host $_
        }
    }
}catch{
    Write-Error "Error"
    exit 1
}
