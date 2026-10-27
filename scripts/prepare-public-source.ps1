param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Destination = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) { throw 'Public source destination must be a new directory.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach ($file in @('.gitignore','.gitattributes','CMakeLists.txt','CMakePresets.json','dependencies.lock.json','shell-formats.json','README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $Destination
}
foreach ($folder in @('src','include','cmake','scripts','tests','installer','licenses','docs')) {
    Copy-Item -LiteralPath (Join-Path $root $folder) -Destination $Destination -Recurse
}
$samples = Join-Path $Destination 'samples'
New-Item -ItemType Directory -Path $samples | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'samples') -File |
    Where-Object Extension -NotIn @('.step','.stp','.iges','.igs') |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $samples }
# Original fixtures are read-only. Generate clean CAD fixtures in the export only.
& (Join-Path $root 'build/Release/make-step-samples.exe') $samples
if ($LASTEXITCODE -ne 0) { throw 'Public CAD fixture generation failed.' }
Write-Output ('Prepared public sources without local state: ' + $Destination)
