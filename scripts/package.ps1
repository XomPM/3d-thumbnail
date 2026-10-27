param([string]$BuildId = ('1.0.1-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
if ($BuildId -notmatch '^1\.0\.1-\d{8}-\d{6}$') { throw 'Unexpected build identifier.' }
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build/Release'
$package = Join-Path $root ('out/package-' + $BuildId)
$runtime = Join-Path $package ('versions/' + $BuildId)
if (Test-Path -LiteralPath $package) { throw 'Package directory already exists. Use a new BuildId.' }
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
# Windows PowerShell 5.1 returns a JSON array as one pipeline object; enumerate it explicitly.
$dependencies = @((Get-Content -LiteralPath (Join-Path $build 'runtime-dependencies.json') -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
foreach ($dependency in $dependencies) {
    if ($dependency.Name -notmatch '^(TK[A-Za-z0-9]+|lib3mf|msvcp140(?:_[a-z0-9_]+)?|vcruntime140(?:_[a-z0-9_]+)?|concrt140)\.dll$') {
        throw ('Unexpected distribution dependency: ' + $dependency.Name)
    }
    if ((Get-FileHash -LiteralPath (Join-Path $build $dependency.Name)).Hash -cne $dependency.SHA256) { throw 'Runtime dependency hash mismatch.' }
}
$binaries = @('thumbnail-cli.exe','thumbnail-worker.exe','thumbnail-shell.dll','thumbnail-settings.exe','shell-probe.exe','runtime-dependencies.json') + @($dependencies.Name)
foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination $runtime }
$payload = [ordered]@{
    Product='3DThumbnail'; Schema=1; Version='1.0.1'; BuildId=$BuildId
    Files=@(Get-ChildItem -LiteralPath $runtime -File | Sort-Object Name | ForEach-Object {
        [ordered]@{Name=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
}
$payload | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtime 'payload.json') -Encoding UTF8
foreach ($name in @('LICENSE','README.md','THIRD_PARTY_NOTICES.md','shell-formats.json')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package
}
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $package -Recurse
Copy-Item -LiteralPath (Join-Path $root 'assets/dmind.ico') -Destination $package
$scripts = Join-Path $package 'scripts'
New-Item -ItemType Directory -Path $scripts | Out-Null
foreach ($name in @('ShellRegistration.psm1','installer-registration.ps1','installer-cleanup.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $scripts
}
# Explicit deletions preserve unknown user files. No recursive directory deletion.
$deletions = @()
foreach ($file in Get-ChildItem -LiteralPath $package -File -Recurse | Where-Object { -not $_.FullName.StartsWith($runtime + [IO.Path]::DirectorySeparatorChar) }) {
    $relative = $file.FullName.Substring($package.Length + 1)
    $deletions += ('Delete "$INSTDIR\' + $relative + '"')
}
$deletions += @('RMDir "$INSTDIR\licenses"','RMDir "$INSTDIR\scripts"')
$deletions | Set-Content -LiteralPath (Join-Path $package 'uninstall-files.nsh') -Encoding utf8
$installer = Join-Path $root ('out/3DThumbnail-' + $BuildId + '-x64-Setup.exe')
$compiler = Join-Path $root '.deps/nsis/nsis-3.13/makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Run setup-dependencies.ps1 to prepare NSIS.' }
$compilerArgs = @('/V2', '/INPUTCHARSET', 'UTF8', ('/DPACKAGE_DIR="' + $package + '"'), ('/DBUILD_ID=' + $BuildId),
    ('/DOUTPUT_FILE="' + $installer + '"'), ('"' + (Join-Path $root 'installer/setup.nsi') + '"'))
$process = Start-Process -FilePath $compiler -ArgumentList $compilerArgs -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $package 'compiler.log') -RedirectStandardError (Join-Path $package 'compiler-errors.log')
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath (Join-Path $package 'compiler-errors.log')
    throw 'Installer compilation failed.'
}
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
($hash + '  ' + [IO.Path]::GetFileName($installer)) | Set-Content -LiteralPath ($installer + '.sha256') -Encoding ascii
[pscustomobject]@{Installer=$installer;Package=$package;Runtime=$runtime;SHA256=$hash;Bytes=(Get-Item -LiteralPath $installer).Length} | ConvertTo-Json
