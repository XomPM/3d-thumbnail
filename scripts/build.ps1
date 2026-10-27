param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ build tools were not found.' }
$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Install the Visual Studio C++ CMake tools.' }
Push-Location $projectRoot
try {
    & $cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build --preset release
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    & (Join-Path $PSScriptRoot 'stage-runtime.ps1') -Configuration Release
    if (-not $SkipTests) {
        & $ctest --preset release
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
} finally {
    Pop-Location
}
