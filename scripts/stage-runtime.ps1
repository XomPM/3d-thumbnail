param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot ('build\' + $Configuration)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe was not found.' }
$available = @{}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory |
    Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if (-not $crt) { throw 'Visual C++ release redistributable directory was not found.' }
$crtDirectory = Join-Path $crt.FullName 'x64/Microsoft.VC143.CRT'
foreach ($root in @((Join-Path $projectRoot '.deps\occt-minimal\bin'), (Join-Path $projectRoot '.deps\lib3mf'), $crtDirectory)) {
    Get-ChildItem -LiteralPath $root -Filter '*.dll' -Recurse -File | Sort-Object FullName | ForEach-Object {
        $key = $_.Name.ToLowerInvariant()
        if (-not $available.ContainsKey($key)) { $available[$key] = $_.FullName }
    }
}
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($name in @('thumbnail-worker.exe','thumbnail-shell.dll','thumbnail-cli.exe','thumbnail-settings.exe','shell-probe.exe')) {
    $queue.Enqueue((Join-Path $output $name))
}
$copied = @()
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (-not $seen.Add($binary)) { continue }
    $dependencies = & $dumpbin /dependents $binary
    if ($LASTEXITCODE -ne 0) { throw ('Cannot inspect binary: ' + $binary) }
    foreach ($line in $dependencies) {
        if ($line -notmatch '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { continue }
        $name = $Matches[1]
        if ($name -match '^(api-ms-|ext-ms-)') { continue }
        $key = $name.ToLowerInvariant()
        if (-not $available.ContainsKey($key)) {
            if (Test-Path -LiteralPath (Join-Path "$env:WINDIR\System32" $name)) { continue }
            throw ('Runtime dependency not found: ' + $name)
        }
        $source = $available[$key]
        $target = Join-Path $output $name
        if (-not (Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $source).Hash) {
            Copy-Item -LiteralPath $source -Destination $target
        }
        $queue.Enqueue($source)
        $copied += [pscustomobject]@{ Name=$name; SHA256=(Get-FileHash -LiteralPath $source).Hash }
    }
}
$copied | Sort-Object Name -Unique | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'runtime-dependencies.json') -Encoding utf8
Write-Output ('Prepared ' + @($copied | Sort-Object Name -Unique).Count + ' dependency DLLs for local testing.')
