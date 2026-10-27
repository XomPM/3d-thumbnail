param(
    [Parameter(Mandatory)][ValidateSet('Install','Uninstall','Status')][string]$Action,
    [Parameter(Mandatory)][string]$StatePath,
    [string]$RuntimeDirectory,
    [string]$ClassesPath = 'Software\Classes'
)
$ErrorActionPreference = 'Stop'
# A Setup launched from PowerShell 7 can inherit its incompatible module path.
# Resolve installer commands only from this Windows PowerShell process's modules.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
Import-Module (Join-Path $PSScriptRoot 'ShellRegistration.psm1') -Force
try {
    if ($Action -eq 'Status') { Get-ShellRegistrationStatus -StatePath $StatePath -ClassesPath $ClassesPath | ConvertTo-Json -Depth 8; exit 0 }
    if ($Action -eq 'Uninstall') {
        if (Test-Path -LiteralPath $StatePath) { Uninstall-ShellRegistration -StatePath $StatePath -ClassesPath $ClassesPath | Out-Null }
    } else {
        $RuntimeDirectory = [IO.Path]::GetFullPath($RuntimeDirectory)
        $payload = Get-Content -LiteralPath (Join-Path $RuntimeDirectory 'payload.json') -Raw | ConvertFrom-Json
        if ($payload.Product -cne '3DThumbnail' -or $payload.Schema -ne 1) { throw 'Invalid installation payload.' }
        foreach ($file in $payload.Files) {
            if ($file.Name -notmatch '^[a-zA-Z0-9_.-]+$') { throw 'Invalid payload filename.' }
            $path = Join-Path $RuntimeDirectory $file.Name
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $file.SHA256) { throw ('Installation file verification failed: ' + $file.Name) }
        }
        $dll = Join-Path $RuntimeDirectory 'thumbnail-shell.dll'
        $status = Get-ShellRegistrationStatus -StatePath $StatePath -ClassesPath $ClassesPath
        if ($status.Status -eq 'Installed') {
            Update-ShellRegistration -DllPath $dll -StatePath $StatePath -ClassesPath $ClassesPath | Out-Null
        } else {
            if ($status.Status -notin @('NotInstalled','Uninstalled','RolledBack')) {
                Uninstall-ShellRegistration -StatePath $StatePath -ClassesPath $ClassesPath | Out-Null
            }
            $manifest = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'shell-formats.json') -Raw | ConvertFrom-Json
            $extensions = @($manifest.formats | ForEach-Object { $_.extensions })
            $bindings = @()
            if ($ClassesPath -eq 'Software\Classes') {
                $probe = Join-Path $RuntimeDirectory 'shell-probe.exe'
                foreach ($extension in $extensions) {
                    $ErrorActionPreference = 'Continue'
                    try { $id = & $probe --progid $extension 2>$null }
                    finally { $ErrorActionPreference = 'Stop' }
                    if ($LASTEXITCODE -ne 0 -or -not $id) { continue }
                    $id = ([string]$id).Trim()
                    if ($id -notmatch '^[A-Za-z0-9][A-Za-z0-9._ -]{0,200}$') { throw 'Invalid effective ProgID.' }
                    $key = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey($id + '\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}')
                    if ($key) {
                        try { if ($key.GetValue('')) { $bindings += [pscustomobject]@{Extension=$extension;ProgId=$id} } }
                        finally { $key.Dispose() }
                    }
                }
            }
            Install-ShellRegistration -DllPath $dll -StatePath $StatePath -ClassesPath $ClassesPath -Extensions $extensions -ProgIdBindings $bindings -AllowInProcessBridge | Out-Null
        }
    }
    if ($ClassesPath -eq 'Software\Classes') {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ThumbnailShellNotify {
    [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b);
}
'@
        [ThumbnailShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    Write-Output ($Action + ' completed. Default opening applications were preserved.')
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
