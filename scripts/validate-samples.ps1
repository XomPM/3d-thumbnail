param([int]$Size = 512)
$ErrorActionPreference = 'Stop'
if ($Size -lt 64 -or $Size -gt 1024) { throw 'Size must be between 64 and 1024.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $projectRoot 'build\Release\thumbnail-cli.exe'
if (-not (Test-Path -LiteralPath $cli)) { throw 'Run scripts/build.ps1 first.' }
$runName = 'validation-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$destination = Join-Path $projectRoot ('out\' + $runName)
New-Item -ItemType Directory -Path $destination | Out-Null
$files = @('bearing_binary.stl', 'bearing.obj', 'bearing.3mf', 'bearing_ap214.step')
$measurements = foreach ($file in $files) {
    $source = Join-Path $projectRoot ('samples\' + $file)
    $output = Join-Path $destination ($file + '.png')
    $before = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $diagnostic = & $cli $source $output --size $Size
    $exitCode = $LASTEXITCODE
    $timer.Stop()
    if ($exitCode -ne 0) { throw ('Sample render failed: ' + $file + ' / ' + ($diagnostic -join ' ')) }
    $after = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($before -ne $after) { throw ('Source file changed: ' + $file) }
    [pscustomobject]@{
        Input = $file
        InputSHA256 = $before
        InputUnchanged = $true
        Output = $output.Substring($projectRoot.Length + 1)
        OutputSHA256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
        Size = $Size
        TotalMilliseconds = $timer.ElapsedMilliseconds
        Diagnostic = $diagnostic -join "`n"
    }
}
$measurements | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $destination 'measurements.json') -Encoding utf8
$measurements | Select-Object Input, TotalMilliseconds, InputUnchanged | Format-Table -AutoSize
Write-Output ('Results: ' + $destination)
