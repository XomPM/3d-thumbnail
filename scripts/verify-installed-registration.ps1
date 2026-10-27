param([ValidateSet('Before','Uninstalled','Reinstalled')][string]$Phase, [Parameter(Mandatory)][string]$StatePath, [Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$journal = Get-Content $StatePath -Raw | ConvertFrom-Json
$defaults = foreach ($extension in ($journal.Extensions | Sort-Object)) {
    $choice = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$extension\UserChoice")
    $association = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey($extension)
    try {
        $values = if ($choice) { foreach ($name in ($choice.GetValueNames() | Sort-Object)) {
            [pscustomobject]@{Name=$name;Kind=$choice.GetValueKind($name).ToString();Data=$choice.GetValue($name)}
        }} else { @() }
        [pscustomobject]@{Extension=$extension;UserChoice=@($values);DefaultProgId= $(if ($association) {$association.GetValue('')})}
    } finally { if ($choice) {$choice.Dispose()}; if ($association) {$association.Dispose()} }
}
$defaultJson = ConvertTo-Json -InputObject @($defaults) -Depth 6 -Compress
if ($Phase -eq 'Before') {
    $defaultJson | Set-Content -LiteralPath (Join-Path $evidence 'defaults-before.json') -Encoding UTF8
    Copy-Item -LiteralPath $StatePath -Destination (Join-Path $evidence 'installed-before.json')
    Write-Output 'Captured current file associations and registration journal.'
    exit 0
}
$defaultsUnchanged = $defaultJson -ceq (Get-Content (Join-Path $evidence 'defaults-before.json') -Raw).Trim()
if (-not $defaultsUnchanged) { throw 'Default file association changed.' }
if ($Phase -eq 'Uninstalled') {
    if ($journal.Status -ne 'Uninstalled') { throw 'Expected uninstalled journal.' }
    $results = foreach ($entry in $journal.Entries) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Classes\' + $entry.SubKey)
        try {
            $keyExists = $null -ne $key
            $valueExists = $keyExists -and ($entry.Name -in $key.GetValueNames())
            $kind = if ($valueExists) {$key.GetValueKind($entry.Name).ToString()} else {$null}
            $data = if ($valueExists) {$key.GetValue($entry.Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)} else {$null}
            if ($kind -in @('Binary','None')) { $data = [Convert]::ToBase64String($data) }
            $matches = ($keyExists -eq $entry.Original.KeyExists) -and ($valueExists -eq $entry.Original.ValueExists) -and
                ($kind -ceq $entry.Original.Kind) -and ((ConvertTo-Json -InputObject $data -Compress) -ceq (ConvertTo-Json -InputObject $entry.Original.Data -Compress))
            [pscustomobject]@{SubKey=$entry.SubKey;Name=$entry.Name;OriginalRestored=$matches}
        } finally { if ($key) {$key.Dispose()} }
    }
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'restoration.json') -Encoding UTF8
    if (@($results | Where-Object {-not $_.OriginalRestored}).Count) { throw 'Some registration values were not restored.' }
    [pscustomobject]@{Status=$journal.Status;RestoredValues=@($results).Count;DefaultsUnchanged=$defaultsUnchanged} | ConvertTo-Json
} else {
    Import-Module (Join-Path $root 'scripts\ShellRegistration.psm1') -Force
    $status = Get-ShellRegistrationStatus -StatePath $StatePath
    if ($status.Status -ne 'Installed' -or @($status.Entries | Where-Object {-not $_.Owned}).Count) { throw 'Reinstall verification failed.' }
    $result = [pscustomobject]@{Status=$status.Status;OwnedValues=$status.Entries.Count;DefaultsUnchanged=$defaultsUnchanged;Runtime=$status.DllPath}
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'reinstalled.json') -Encoding UTF8
    $result | ConvertTo-Json
}
