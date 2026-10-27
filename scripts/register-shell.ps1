param(
    [ValidateSet('Status','Plan','Install','Uninstall')][string]$Action = 'Status',
    [string[]]$Extensions = @('.stl','.obj','.3mf','.step','.stp','.ply','.glb','.gltf','.iges','.igs','.fbx'),
    [switch]$AllowInProcessBridge
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ShellRegistration.psm1') -Force
$projectRoot = Split-Path -Parent $PSScriptRoot
$local = Join-Path $projectRoot '.local'
$journal = Join-Path $local 'shell-registration.json'

function Get-CurrentProgIdBindings {
    $probe = Join-Path $projectRoot 'build\Release\shell-probe.exe'
    if (-not (Test-Path -LiteralPath $probe)) { throw 'Build shell-probe.exe before inspecting or installing a registration plan.' }
    foreach ($extension in $Extensions) {
        $progId = & $probe --progid $extension 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $progId) { continue }
        $progId = ([string]$progId).Trim()
        if ($progId -notmatch '^[A-Za-z0-9][A-Za-z0-9._ -]{0,200}$') { throw 'Invalid effective ProgID.' }
        $handler = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey($progId + '\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}')
        if ($handler) {
            try { if ($handler.GetValue('')) { [pscustomobject]@{Extension=$extension;ProgId=$progId} } }
            finally { $handler.Dispose() }
        }
    }
}

function Notify-Shell {
    if (-not ('ThreeDThumbnail.ShellChange' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace ThreeDThumbnail {
    public static class ShellChange {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr first, IntPtr second);
    }
}
'@
    }
    [ThreeDThumbnail.ShellChange]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

switch ($Action) {
    'Status' { Get-ShellRegistrationStatus -StatePath $journal | ConvertTo-Json -Depth 8 }
    'Plan' {
        $bindings = @(Get-CurrentProgIdBindings)
        Get-ShellRegistrationPlan -DllPath (Join-Path $projectRoot 'build\Release\thumbnail-shell.dll') -Extensions $Extensions -ProgIdBindings $bindings -AllowInProcessBridge:$AllowInProcessBridge | ConvertTo-Json -Depth 5
    }
    'Install' {
        if (-not $AllowInProcessBridge) {
            throw 'Isolated Shell activation is unresolved on this PC. Review docs/shell-integration.md; the experimental Explorer bridge requires explicit -AllowInProcessBridge opt-in.'
        }
        $status = Get-ShellRegistrationStatus -StatePath $journal
        if ($status.Status -notin @('NotInstalled','Uninstalled','RolledBack')) {
            throw 'An installation already exists. Check Status or Uninstall before installing another build.'
        }
        $build = Join-Path $projectRoot 'build\Release'
        foreach ($name in @('thumbnail-shell.dll','thumbnail-worker.exe','runtime-dependencies.json')) {
            if (-not (Test-Path -LiteralPath (Join-Path $build $name))) { throw ('Build output missing: ' + $name) }
        }
        $runtime = Join-Path $local ('runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
        New-Item -ItemType Directory -Path $runtime -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $build 'thumbnail-worker.exe') -Destination $runtime
        Copy-Item -LiteralPath (Join-Path $build 'thumbnail-shell.dll') -Destination $runtime
        $dependencies = Get-Content -LiteralPath (Join-Path $build 'runtime-dependencies.json') -Raw | ConvertFrom-Json
        foreach ($dependency in $dependencies) {
            $source = Join-Path $build $dependency.Name
            if ((Get-FileHash -LiteralPath $source).Hash -ne $dependency.SHA256) { throw ('Runtime hash mismatch: ' + $dependency.Name) }
            Copy-Item -LiteralPath $source -Destination $runtime
        }
        Copy-Item -LiteralPath (Join-Path $build 'runtime-dependencies.json') -Destination $runtime
        $bindings = @(Get-CurrentProgIdBindings)
        $state = Install-ShellRegistration -DllPath (Join-Path $runtime 'thumbnail-shell.dll') -StatePath $journal -Extensions $Extensions -ProgIdBindings @($bindings) -AllowInProcessBridge:$AllowInProcessBridge
        Notify-Shell
        [pscustomobject]@{Status=$state.Status;Extensions=$state.Extensions;Journal=$journal;Runtime=$runtime} | ConvertTo-Json
    }
    'Uninstall' {
        $state = Uninstall-ShellRegistration -StatePath $journal
        Notify-Shell
        [pscustomobject]@{Status=$state.Status;PreservedChanges=@($state.Entries | Where-Object Result -eq 'PreservedExternalChange').Count;Journal=$journal} | ConvertTo-Json
    }
}
