param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $projectRoot 'build\Release\shell-probe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw 'Build shell-probe before validation.' }
$output = Join-Path $projectRoot ('out\shell-validation-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$models = Join-Path $output 'models'
New-Item -ItemType Directory -Path $models -ErrorAction Stop | Out-Null
$measurements = [System.Collections.Generic.List[object]]::new()
Add-Type -AssemblyName System.Drawing

function Compare-Thumbnail([string]$First, [string]$Second) {
    $left = [System.Drawing.Bitmap]::new((Join-Path $output ($First + '.png')))
    $right = $null
    try {
        $right = [System.Drawing.Bitmap]::new((Join-Path $output ($Second + '.png')))
        if ($left.Width -ne $right.Width -or $left.Height -ne $right.Height) { throw 'Thumbnail dimensions differ.' }
        [double]$difference = 0
        $count = 0
        for ($y = 0; $y -lt $left.Height; $y += 4) {
            for ($x = 0; $x -lt $left.Width; $x += 4) {
                $a = $left.GetPixel($x, $y)
                $b = $right.GetPixel($x, $y)
                $difference += [Math]::Abs([int]$a.R - $b.R) + [Math]::Abs([int]$a.G - $b.G) + [Math]::Abs([int]$a.B - $b.B)
                $count += 3
            }
        }
        return $difference / $count
    } finally {
        if ($right) { $right.Dispose() }
        $left.Dispose()
    }
}

function Save-Measurements {
    @($measurements.ToArray()) | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
}

function Measure-Extraction([string]$InputPath, [string]$Label, [switch]$ExpectFailure) {
    $destination = Join-Path $output ($Label + '.png')
    $before = (Get-FileHash -LiteralPath $InputPath -Algorithm SHA256).Hash
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    # Native errors are captured as text; the process exit code determines success.
    try {
        $ErrorActionPreference = 'Continue'
        $log = @(& $probe --shell $InputPath $destination 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = 'Stop' }
    $timer.Stop()
    $after = (Get-FileHash -LiteralPath $InputPath -Algorithm SHA256).Hash
    $imageHash = if (Test-Path -LiteralPath $destination) { (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash } else { $null }
    $measurements.Add([pscustomobject]@{
        Label=$Label; Input=$InputPath; InputSha256=$before; InputUnchanged=($before -eq $after)
        ExitCode=$exitCode; Milliseconds=$timer.ElapsedMilliseconds; OutputSha256=$imageHash; Diagnostic=$log
    })
    Save-Measurements
    if ($before -ne $after) { throw "Shell extraction modified input: $Label" }
    if ($ExpectFailure) {
        if ($exitCode -eq 0 -or $imageHash) { throw "Corrupt input returned a thumbnail: $Label" }
    } elseif ($exitCode -ne 0 -or -not $imageHash) { throw "Shell extraction failed: $Label ($($log -join ' '))" }
    return $imageHash
}

function Notify-Model([string]$Path) {
    & $probe --notify $Path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Shell update notification failed.' }
}

$samples = @(
    @('bearing_binary.stl', '01 베어링.stl'),
    @('bearing.obj', '02 베어링.obj'),
    @('bearing.3mf', '03 베어링.3mf'),
    @('placed_solids.step', '04 조립부품.step'),
    @('bearing_ap203.stp', '05 베어링.stp'),
    @('bearing.ply', '06 베어링.ply'),
    @('placed_meshes.glb', '07 조립부품.glb'),
    @('bearing.gltf', '08 외부 버퍼.gltf'),
    @('bearing.iges', '09 베어링.iges'),
    @('bearing_brep.igs', '10 베어링.igs'),
    @('bearing.fbx', '11 베어링.fbx')
)
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\bearing.bin') -Destination (Join-Path $models 'bearing.bin')
$bufferHash = (Get-FileHash -LiteralPath (Join-Path $models 'bearing.bin')).Hash
foreach ($sample in $samples) {
    $destination = Join-Path $models $sample[1]
    Copy-Item -LiteralPath (Join-Path $projectRoot ('samples\' + $sample[0])) -Destination $destination
    Measure-Extraction $destination $sample[0] | Out-Null
}

# Only this newly created test copy is changed, never the samples or user files.
$changing = Join-Path $models '12 내용변경 검증.step'
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\bearing_ap214.step') -Destination $changing
Measure-Extraction $changing 'cache-single' | Out-Null
Measure-Extraction $changing 'cache-repeat' | Out-Null
# Windows may re-encode cached thumbnails. Compare decoded RGB samples, not PNG bytes.
$repeatDifference = Compare-Thumbnail 'cache-single' 'cache-repeat'
if ($repeatDifference -gt 5) { throw 'Unchanged model returned a different image.' }

Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\placed_solids.step') -Destination $changing -Force
[System.IO.File]::SetLastWriteTimeUtc($changing, [DateTime]::UtcNow)
Notify-Model $changing
Measure-Extraction $changing 'cache-assembly' | Out-Null
$changeDifference = Compare-Thumbnail 'cache-single' 'cache-assembly'
if ($changeDifference -lt 15) { throw 'Model change returned the previous thumbnail.' }

[System.IO.File]::WriteAllText($changing, 'invalid STEP test data')
Notify-Model $changing
Measure-Extraction $changing 'cache-corrupt' -ExpectFailure | Out-Null

Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\placed_solids.step') -Destination $changing -Force
[System.IO.File]::SetLastWriteTimeUtc($changing, [DateTime]::UtcNow)
Notify-Model $changing
Measure-Extraction $changing 'cache-recovered' | Out-Null
$recoveryDifference = Compare-Thumbnail 'cache-assembly' 'cache-recovered'
if ($recoveryDifference -gt 5) { throw 'Repaired model did not recover its thumbnail.' }
if ((Get-FileHash -LiteralPath (Join-Path $models 'bearing.bin')).Hash -ne $bufferHash) { throw 'External buffer was modified.' }

[pscustomobject]@{
    Metric='Mean absolute RGB difference at every fourth pixel (0..255)'
    Repeat=$repeatDifference; ContentChange=$changeDifference; Recovery=$recoveryDifference
    SimilarityMaximum=5; ContentChangeMinimum=15
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'image-comparison.json') -Encoding UTF8

[pscustomobject]@{Status='Passed'; Cases=$measurements.Count; Output=$output; ExplorerFolder=$models} | ConvertTo-Json
