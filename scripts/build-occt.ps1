param([int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root '.deps/occt-source/OCCT-7_9_3'
$build = Join-Path $root '.deps/occt-build'
$install = Join-Path $root '.deps/occt-minimal'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) { throw 'Run setup-dependencies.ps1 first.' }
$arguments = @('-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_INSTALL_PREFIX=$install", '-DINSTALL_DIR_BIN=bin', '-DINSTALL_DIR_LIB=lib', '-DINSTALL_DIR_INCLUDE=include',
    '-DBUILD_LIBRARY_TYPE=Shared', '-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDEIGES;TKMesh;TKPrim;TKBO;TKBool',
    '-DBUILD_USE_PCH=ON', '-DUSE_MMGR_TYPE=NATIVE')
foreach ($module in @('FoundationClasses','ModelingData','ModelingAlgorithms','Visualization','ApplicationFramework','DataExchange','DETools','Draw')) {
    $arguments += "-DBUILD_MODULE_${module}=OFF"
}
foreach ($feature in @('TK','FREETYPE','FREEIMAGE','FFMPEG','OPENVR','RAPIDJSON','DRACO','TBB','EIGEN','D3D','OPENGL','GLES2')) {
    $arguments += "-DUSE_${feature}=OFF"
}
& $cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'OCCT configuration failed.' }
& $cmake --build $build --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OCCT build failed.' }
& $cmake --install $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'OCCT install failed.' }
Write-Output 'OCCT prepared without optional third-party graphics, video, VR or allocator libraries.'
