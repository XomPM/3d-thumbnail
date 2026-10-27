param([Parameter(Mandatory)][string]$InstallRoot)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$allowed = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs/3DThumbnail')).TrimEnd('\')
$resolved = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
if ($resolved -ine $allowed) { throw 'Unexpected product directory; cleanup refused.' }
function Assert-OrdinaryDirectory([string]$Path) {
    $item = Get-Item -LiteralPath $Path
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked product directory; cleanup refused.' }
}
Assert-OrdinaryDirectory $resolved
$versions = Join-Path $resolved 'versions'
$locked = 0
if (Test-Path -LiteralPath $versions) {
    Assert-OrdinaryDirectory $versions
    foreach ($version in Get-ChildItem -LiteralPath $versions -Directory) {
        if ($version.Name -notmatch '^\d+\.\d+\.\d+-\d{8}-\d{6}$') { continue }
        Assert-OrdinaryDirectory $version.FullName
        $manifestPath = Join-Path $version.FullName 'payload.json'
        if (-not (Test-Path -LiteralPath $manifestPath)) { continue }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.Product -cne '3DThumbnail' -or $manifest.Schema -ne 1) { throw 'Unknown product payload; cleanup refused.' }
        foreach ($file in $manifest.Files) {
            if ($file.Name -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid cleanup filename.' }
            $path = Join-Path $version.FullName $file.Name
            if (Test-Path -LiteralPath $path) {
                try { Remove-Item -LiteralPath $path -ErrorAction Stop }
                catch [IO.IOException] { ++$locked }
                catch [UnauthorizedAccessException] { ++$locked }
            }
        }
        if (@(Get-ChildItem -LiteralPath $version.FullName -Force | Where-Object Name -ne 'payload.json').Count -eq 0) {
            Remove-Item -LiteralPath $manifestPath
            [IO.Directory]::Delete($version.FullName, $false)
        }
    }
    if (@(Get-ChildItem -LiteralPath $versions -Force).Count -eq 0) { [IO.Directory]::Delete($versions, $false) }
}
Write-Output ('Removed product files. Files still in use: ' + $locked + '. Registration has already been removed.')
