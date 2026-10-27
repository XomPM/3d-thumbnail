Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$thumbnailCategory = '{E357FCCD-A995-4576-B01F-234630154E96}'

function Assert-ClassesPath([string]$ClassesPath) {
    if ($ClassesPath -ne 'Software\Classes' -and $ClassesPath -notmatch '^Software\\3DThumbnailTests\\[0-9a-f-]{36}\\Classes$') {
        throw 'Registration is restricted to current-user Classes or a dedicated test root.'
    }
}

function Get-ShellRegistrationPlan {
    param([string]$DllPath, [string[]]$Extensions = @('.stl','.obj','.3mf','.step','.stp','.ply','.glb','.gltf','.iges','.igs','.fbx'),
          [object[]]$ProgIdBindings = @(), [int]$Schema = 4, [switch]$AllowInProcessBridge)
    $manifest = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'shell-formats.json') -Raw | ConvertFrom-Json
    $ProgIdBindings = @($ProgIdBindings | Where-Object { $null -ne $_ })
    $selected = @($Extensions | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique)
    $supported = @($manifest.formats | ForEach-Object { $_.extensions })
    if (-not $selected.Count -or @($selected | Where-Object { $_ -notin $supported }).Count) { throw 'Unsupported extension selection.' }
    foreach ($binding in $ProgIdBindings) {
        if ($binding.Extension -notin $selected -or $binding.ProgId -notmatch '^[A-Za-z0-9][A-Za-z0-9._ -]{0,200}$') { throw 'Invalid ProgID binding.' }
    }
    foreach ($format in $manifest.formats) {
        $chosen = @($format.extensions | Where-Object { $_ -in $selected })
        if (-not $chosen.Count) { continue }
        $class = 'CLSID\' + $format.clsid
        [pscustomobject]@{ SubKey=$class; Name=''; Desired=('3D Thumbnail ' + $format.name) }
        [pscustomobject]@{ SubKey=($class + '\InprocServer32'); Name=''; Desired=$DllPath }
        [pscustomobject]@{ SubKey=($class + '\InprocServer32'); Name='ThreadingModel'; Desired='Apartment' }
        if ($Schema -eq 2) {
            [pscustomobject]@{ SubKey=$class; Name='AppID'; Desired=$format.clsid }
            [pscustomobject]@{ SubKey=('AppID\' + $format.clsid); Name='DllSurrogate'; Desired='' }
        }
        if ($Schema -eq 3 -or ($Schema -ge 4 -and $AllowInProcessBridge)) {
            [pscustomobject]@{ SubKey=$class; Name='DisableProcessIsolation'; Desired=1; DesiredKind='DWord' }
        }
        foreach ($extension in $chosen) {
            foreach ($prefix in @('', 'SystemFileAssociations\')) {
                [pscustomobject]@{ SubKey=($prefix + $extension + '\ShellEx\' + $thumbnailCategory); Name=''; Desired=$format.clsid }
            }
        }
        foreach ($binding in @($ProgIdBindings | Where-Object { $_.Extension -in $chosen })) {
            [pscustomobject]@{ SubKey=($binding.ProgId + '\ShellEx\' + $thumbnailCategory); Name=''; Desired=$format.clsid }
        }
    }
}

function Get-DesiredKind($Entry) {
    if ($Entry.PSObject.Properties['DesiredKind']) { return $Entry.DesiredKind }
    return 'String'
}

function Get-RegistrySnapshot([string]$Path, [string]$Name) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path)
    if (-not $key) { return [pscustomobject]@{ KeyExists=$false; ValueExists=$false; Kind=$null; Data=$null } }
    try {
        if ($Name -notin $key.GetValueNames()) { return [pscustomobject]@{ KeyExists=$true; ValueExists=$false; Kind=$null; Data=$null } }
        $kind = $key.GetValueKind($Name).ToString()
        $data = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($kind -in @('Binary','None')) { $data = [Convert]::ToBase64String($data) }
        return [pscustomobject]@{ KeyExists=$true; ValueExists=$true; Kind=$kind; Data=$data }
    } finally { $key.Dispose() }
}

function Set-RegistrySnapshot([string]$Path, [string]$Name, $Snapshot) {
    if (-not $Snapshot.ValueExists) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path, $true)
        if ($key) { try { $key.DeleteValue($Name, $false) } finally { $key.Dispose() } }
        return
    }
    $data = $Snapshot.Data
    switch ($Snapshot.Kind) {
        { $_ -in @('Binary','None') } { $data = [Convert]::FromBase64String($data) }
        'DWord' { $data = [int]$data }
        'QWord' { $data = [long]$data }
        'MultiString' { $data = [string[]]$data }
        default { $data = [string]$data }
    }
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($Path)
    try { $key.SetValue($Name, $data, [Microsoft.Win32.RegistryValueKind]::$($Snapshot.Kind)) }
    finally { $key.Dispose() }
}

function Save-RegistrationState([string]$Path, $State) {
    $temporary = $Path + '.' + [guid]::NewGuid().ToString() + '.tmp'
    $bytes = [Text.Encoding]::UTF8.GetBytes(($State | ConvertTo-Json -Depth 12))
    $file = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $file.Write($bytes, 0, $bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
    if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $Path) }
}

function Open-RegistrationLock([string]$StatePath) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($StatePath)) | Out-Null
    return [IO.File]::Open(($StatePath + '.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}

function Read-RegistrationState([string]$StatePath, [string]$ClassesPath) {
    $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    if ($state.Schema -notin @(1,2,3,4) -or $state.ClassesPath -ne $ClassesPath -or
        $state.UserSid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value) { throw 'Registration journal identity mismatch.' }
    $bindings = if ($state.Schema -ge 2) { @($state.ProgIdBindings) } else { @() }
    $allowBridge = $state.Schema -ge 4 -and $state.AllowInProcessBridge
    $expected = @(Get-ShellRegistrationPlan -DllPath $state.DllPath -Extensions $state.Extensions -ProgIdBindings $bindings -Schema $state.Schema -AllowInProcessBridge:$allowBridge)
    $previousPlan = @(if ($state.PSObject.Properties['PreviousDllPath']) {
        Get-ShellRegistrationPlan -DllPath $state.PreviousDllPath -Extensions $state.Extensions -ProgIdBindings $bindings -Schema $state.Schema -AllowInProcessBridge:$allowBridge
    })
    if ($state.Entries.Count -ne $expected.Count) { throw 'Unexpected journal entries.' }
    for ($index = 0; $index -lt $expected.Count; $index++) {
        $entry = $state.Entries[$index]; $plan = $expected[$index]
        if ($entry.PSObject.Properties['PreviousDesired'] -and
            ($previousPlan.Count -ne $expected.Count -or $entry.PreviousDesired -cne $previousPlan[$index].Desired)) {
            throw 'Unexpected previous registration value in update journal.'
        }
        if ($previousPlan.Count -and -not $entry.PSObject.Properties['PreviousDesired']) { throw 'Incomplete update journal.' }
        if ($entry.SubKey -cne $plan.SubKey -or $entry.Name -cne $plan.Name -or $entry.Desired -cne $plan.Desired -or
            (Get-DesiredKind $entry) -ne (Get-DesiredKind $plan)) {
            throw 'Unexpected registration journal path or desired value.'
        }
    }
    foreach ($path in $state.CreatedKeys) {
        if (-not @($expected | Where-Object { $_.SubKey -eq $path -or $_.SubKey.StartsWith($path + '\', [StringComparison]::OrdinalIgnoreCase) }).Count) {
            throw 'Unexpected created registry key in journal.'
        }
    }
    return $state
}

function Restore-RegistrationEntries($State) {
    foreach ($entry in $State.Entries) {
        $path = $State.ClassesPath + '\' + $entry.SubKey
        $current = Get-RegistrySnapshot $path $entry.Name
        $owned = $current.Data -ceq $entry.Desired
        if ($entry.PSObject.Properties['PreviousDesired']) { $owned = $owned -or $current.Data -ceq $entry.PreviousDesired }
        if ($current.ValueExists -and $current.Kind -eq (Get-DesiredKind $entry) -and $owned) {
            Set-RegistrySnapshot $path $entry.Name $entry.Original
            $entry.Result = 'Restored'
        } else { $entry.Result = 'PreservedExternalChange' }
    }
    foreach ($relative in @($State.CreatedKeys | Sort-Object { $_.Length } -Descending)) {
        $path = $State.ClassesPath + '\' + $relative
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($path)
        if (-not $key) { continue }
        try { $empty = $key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0 } finally { $key.Dispose() }
        if ($empty) { [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($path, $false) }
    }
}

function Install-ShellRegistration {
    param([string]$DllPath, [string]$StatePath, [string[]]$Extensions = @('.stl','.obj','.3mf','.step','.stp','.ply','.glb','.gltf','.iges','.igs','.fbx'),
          [string]$ClassesPath = 'Software\Classes', [object[]]$ProgIdBindings = @(), [switch]$AllowInProcessBridge)
    Assert-ClassesPath $ClassesPath
    $DllPath = [IO.Path]::GetFullPath($DllPath)
    $ProgIdBindings = @($ProgIdBindings | Where-Object { $null -ne $_ })
    $StatePath = [IO.Path]::GetFullPath($StatePath)
    if (-not [IO.File]::Exists($DllPath)) { throw 'Shell DLL does not exist.' }
    $lock = Open-RegistrationLock $StatePath
    try {
        if (Test-Path -LiteralPath $StatePath) {
            $previous = Read-RegistrationState $StatePath $ClassesPath
            if ($previous.Status -notin @('Uninstalled','RolledBack')) { throw 'An active registration journal exists. Use status or uninstall before replacing it.' }
            [IO.File]::Copy($StatePath, ($StatePath + '.history-' + [guid]::NewGuid().ToString() + '.json'))
        }
        $plan = @(Get-ShellRegistrationPlan -DllPath $DllPath -Extensions $Extensions -ProgIdBindings $ProgIdBindings -AllowInProcessBridge:$AllowInProcessBridge)
        foreach ($group in @($plan | Group-Object SubKey,Name)) {
            if ($group.Count -gt 1) { throw 'Ambiguous shared ProgID registration; no changes were made.' }
        }
        $created = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $entries = foreach ($item in $plan) {
            $segments = $item.SubKey.Split('\'); $relative = ''
            foreach ($segment in $segments) {
                $relative = if ($relative) { $relative + '\' + $segment } else { $segment }
                $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($ClassesPath + '\' + $relative)
                if ($key) { $key.Dispose() } else { [void]$created.Add($relative) }
            }
            [pscustomobject]@{ SubKey=$item.SubKey; Name=$item.Name; Desired=$item.Desired; DesiredKind=(Get-DesiredKind $item);
                Original=(Get-RegistrySnapshot ($ClassesPath + '\' + $item.SubKey) $item.Name); Result='Pending' }
        }
        $state = [pscustomobject]@{ Schema=4; Status='Installing'; ClassesPath=$ClassesPath; DllPath=$DllPath; ProgIdBindings=@($ProgIdBindings); AllowInProcessBridge=[bool]$AllowInProcessBridge;
            UserSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;
            Extensions=@($Extensions); CreatedKeys=@($created); Entries=@($entries); CreatedAt=[DateTime]::UtcNow.ToString('o') }
        # Persist the complete original state before the first registry mutation.
        Save-RegistrationState $StatePath $state
        try {
            foreach ($entry in $state.Entries) {
                Set-RegistrySnapshot ($ClassesPath + '\' + $entry.SubKey) $entry.Name ([pscustomobject]@{ValueExists=$true;Kind=(Get-DesiredKind $entry);Data=$entry.Desired})
                $entry.Result = 'Installed'
            }
            $state.Status = 'Installed'
            Save-RegistrationState $StatePath $state
        } catch {
            $failure = $_
            Restore-RegistrationEntries $state
            $state.Status = 'RolledBack'
            Save-RegistrationState $StatePath $state
            throw $failure
        }
        return $state
    } finally { $lock.Dispose() }
}

function Update-ShellRegistration {
    param([string]$DllPath, [string]$StatePath, [string]$ClassesPath = 'Software\Classes')
    Assert-ClassesPath $ClassesPath
    $DllPath = [IO.Path]::GetFullPath($DllPath)
    $StatePath = [IO.Path]::GetFullPath($StatePath)
    if (-not [IO.File]::Exists($DllPath)) { throw 'Updated Shell DLL does not exist.' }
    $lock = Open-RegistrationLock $StatePath
    try {
        $previous = Read-RegistrationState $StatePath $ClassesPath
        if ($previous.Status -ne 'Installed' -or $previous.Schema -ne 4 -or $previous.PSObject.Properties['PreviousDllPath']) {
            throw 'Recover or uninstall the incomplete registration before updating.'
        }
        foreach ($entry in $previous.Entries) {
            $current = Get-RegistrySnapshot ($ClassesPath + '\' + $entry.SubKey) $entry.Name
            if (-not $current.ValueExists -or $current.Kind -ne (Get-DesiredKind $entry) -or $current.Data -cne $entry.Desired) {
                throw 'Another application changed thumbnail registration. Uninstall first; external changes will be preserved.'
            }
        }
        if ($DllPath -ceq $previous.DllPath) { return $previous }
        [IO.File]::Copy($StatePath, ($StatePath + '.history-' + [guid]::NewGuid().ToString() + '.json'))
        $state = $previous | ConvertTo-Json -Depth 12 | ConvertFrom-Json
        $state | Add-Member -NotePropertyName PreviousDllPath -NotePropertyValue $previous.DllPath
        $state.DllPath = $DllPath
        $state.Status = 'Updating'
        $plan = @(Get-ShellRegistrationPlan -DllPath $DllPath -Extensions $state.Extensions -ProgIdBindings @($state.ProgIdBindings) -AllowInProcessBridge:$state.AllowInProcessBridge)
        for ($index = 0; $index -lt $plan.Count; $index++) {
            $state.Entries[$index] | Add-Member -NotePropertyName PreviousDesired -NotePropertyValue $previous.Entries[$index].Desired
            $state.Entries[$index].Desired = $plan[$index].Desired
        }
        # Both old and new owned values can be restored if the process ends mid-update.
        Save-RegistrationState $StatePath $state
        try {
            foreach ($entry in $state.Entries) {
                $current = Get-RegistrySnapshot ($ClassesPath + '\' + $entry.SubKey) $entry.Name
                if (-not $current.ValueExists -or $current.Kind -ne (Get-DesiredKind $entry) -or $current.Data -cne $entry.PreviousDesired) {
                    throw 'Registration changed during update; rolling back owned values.'
                }
                Set-RegistrySnapshot ($ClassesPath + '\' + $entry.SubKey) $entry.Name ([pscustomobject]@{ValueExists=$true;Kind=(Get-DesiredKind $entry);Data=$entry.Desired})
            }
            foreach ($entry in $state.Entries) { $entry.PSObject.Properties.Remove('PreviousDesired') }
            $state.PSObject.Properties.Remove('PreviousDllPath')
            $state.Status = 'Installed'
            Save-RegistrationState $StatePath $state
            return $state
        } catch {
            $failure = $_
            for ($index = 0; $index -lt $plan.Count; $index++) {
                $entry = $plan[$index]
                $path = $ClassesPath + '\' + $entry.SubKey
                $current = Get-RegistrySnapshot $path $entry.Name
                if ($current.ValueExists -and $current.Kind -eq (Get-DesiredKind $entry) -and $current.Data -ceq $entry.Desired) {
                    Set-RegistrySnapshot $path $entry.Name ([pscustomobject]@{ValueExists=$true;Kind=(Get-DesiredKind $entry);Data=$previous.Entries[$index].Desired})
                }
            }
            Save-RegistrationState $StatePath $previous
            throw $failure
        }
    } finally { $lock.Dispose() }
}

function Uninstall-ShellRegistration {
    param([string]$StatePath, [string]$ClassesPath = 'Software\Classes')
    Assert-ClassesPath $ClassesPath
    $StatePath = [IO.Path]::GetFullPath($StatePath)
    $lock = Open-RegistrationLock $StatePath
    try {
        $state = Read-RegistrationState $StatePath $ClassesPath
        if ($state.Status -notin @('Uninstalled','RolledBack')) {
            Restore-RegistrationEntries $state
            $state.Status = 'Uninstalled'
            Save-RegistrationState $StatePath $state
        }
        return $state
    } finally { $lock.Dispose() }
}

function Get-ShellRegistrationStatus {
    param([string]$StatePath, [string]$ClassesPath = 'Software\Classes')
    Assert-ClassesPath $ClassesPath
    if (-not (Test-Path -LiteralPath $StatePath)) { return [pscustomobject]@{Status='NotInstalled'; Entries=@()} }
    $state = Read-RegistrationState $StatePath $ClassesPath
    $values = foreach ($entry in $state.Entries) {
        $current = Get-RegistrySnapshot ($ClassesPath + '\' + $entry.SubKey) $entry.Name
        [pscustomobject]@{SubKey=$entry.SubKey; Name=$entry.Name; Desired=$entry.Desired; Current=$current;
            Owned=($current.ValueExists -and $current.Kind -eq (Get-DesiredKind $entry) -and $current.Data -ceq $entry.Desired)}
    }
    return [pscustomobject]@{ Status=$state.Status; DllPath=$state.DllPath; Extensions=$state.Extensions; Entries=@($values) }
}
Export-ModuleMember -Function Get-ShellRegistrationPlan,Install-ShellRegistration,Update-ShellRegistration,Uninstall-ShellRegistration,Get-ShellRegistrationStatus
