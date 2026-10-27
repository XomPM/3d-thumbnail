$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$downloadRoot = Join-Path $dependencyRoot 'downloads'
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($package in $manifest.packages) {
    $archive = Join-Path $downloadRoot ($package.name + '-' + $package.version + '.zip')
    $destination = Join-Path $dependencyRoot $package.name
    $stamp = Join-Path $destination '.verified-sha256'
    if ((Test-Path -LiteralPath $stamp) -and ((Get-Content -LiteralPath $stamp -Raw).Trim() -eq $package.sha256)) {
        Write-Output ($package.name + ' already prepared.')
        continue
    }
    if (-not (Test-Path -LiteralPath $archive)) {
        $partial = $archive + '.partial'
        Write-Output ('Downloading ' + $package.name + ' ' + $package.version)
        & curl.exe --fail --location --silent --show-error --max-time 900 --output $partial $package.url
        if ($LASTEXITCODE -ne 0) { throw ('Download failed: ' + $package.name) }
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
            throw ('SHA-256 mismatch: ' + $package.name)
        }
        Move-Item -LiteralPath $partial -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) {
        throw ('SHA-256 mismatch: ' + $package.name)
    }
    if (Test-Path -LiteralPath $destination) { throw ('Incomplete dependency folder requires inspection: ' + $destination) }
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $allowedPrefix = [System.IO.Path]::GetFullPath($destination) + [System.IO.Path]::DirectorySeparatorChar
        foreach ($entry in $zip.Entries) {
            $target = [System.IO.Path]::GetFullPath((Join-Path $destination $entry.FullName))
            if (-not $target.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw 'Archive contains a path outside its destination.'
            }
        }
    } finally { $zip.Dispose() }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
    Set-Content -LiteralPath $stamp -Value $package.sha256 -Encoding ascii
    Write-Output ('Prepared ' + $package.name)
}
