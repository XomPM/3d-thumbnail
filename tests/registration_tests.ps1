$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\scripts\ShellRegistration.psm1') -Force
$projectRoot = Split-Path -Parent $PSScriptRoot
$identity = [guid]::NewGuid().ToString()
$testRoot = 'Software\3DThumbnailTests\' + $identity
$classes = $testRoot + '\Classes'
$directory = Join-Path $projectRoot ('build\test-output\registration-' + $identity)
New-Item -ItemType Directory -Path $directory | Out-Null
$journal = Join-Path $directory 'state.json'
$dll = Join-Path $projectRoot 'build\Release\thumbnail-shell.dll'
$category = '{E357FCCD-A995-4576-B01F-234630154E96}'
$plan = @(Get-ShellRegistrationPlan -DllPath $dll)

function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Set-TestValue([string]$Relative, [string]$Name, $Data, [Microsoft.Win32.RegistryValueKind]$Kind = 'String') {
    if ($Kind -eq 'MultiString') { $Data = [string[]]$Data }
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($classes + '\' + $Relative)
    try { $key.SetValue($Name, $Data, $Kind) } finally { $key.Dispose() }
}
function Read-TestValue([string]$Relative, [string]$Name = '') {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($classes + '\' + $Relative)
    if (-not $key) { return 'ABSENT-KEY' }
    try {
        if ($Name -notin $key.GetValueNames()) { return 'ABSENT-VALUE' }
        $kind = $key.GetValueKind($Name).ToString()
        $data = $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        return ([ordered]@{Kind=$kind;Data=$data} | ConvertTo-Json -Compress)
    } finally { $key.Dispose() }
}
function Expect-Failure([scriptblock]$Operation) {
    $failed = $false
    try { & $Operation | Out-Null } catch { $failed = $true }
    Assert $failed 'Expected refusal did not happen.'
}

$stl = '.stl\ShellEx\' + $category
$obj = '.obj\ShellEx\' + $category
$threeMf = '.3mf\ShellEx\' + $category
$step = '.step\ShellEx\' + $category
$stp = 'SystemFileAssociations\.stp\ShellEx\' + $category
Set-TestValue $stl '' '{Original-STL-Provider}'
Set-TestValue $stl 'Unrelated' 17 DWord
Set-TestValue $obj '' '%TEMP%\original-provider' ExpandString
Set-TestValue $threeMf '' @('first','second') MultiString
Set-TestValue $step '' 42 DWord
Set-TestValue $stp '' ([byte[]]@(1,2,0,255)) Binary
Set-TestValue '.stl' '' 'Original.OpenWith.Application'
$original = @{}
foreach ($entry in $plan) { $original[$entry.SubKey + '|' + $entry.Name] = Read-TestValue $entry.SubKey $entry.Name }

$state = Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes
Assert ($state.Status -eq 'Installed') 'Install failed.'
$status = Get-ShellRegistrationStatus -StatePath $journal -ClassesPath $classes
Assert (@($status.Entries | Where-Object { -not $_.Owned }).Count -eq 0) 'Installed values differ.'
$before = (Get-FileHash -LiteralPath $journal).Hash
Expect-Failure { Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes }
Assert ((Get-FileHash -LiteralPath $journal).Hash -eq $before) 'Repeated install overwrote original backup.'
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
foreach ($entry in $plan) {
    Assert ((Read-TestValue $entry.SubKey $entry.Name) -ceq $original[$entry.SubKey + '|' + $entry.Name]) ('Round-trip changed ' + $entry.SubKey)
}
Write-Output 'PASS registry round-trip: existing/absent values, ExpandString, MultiString, DWord, Binary; backup protected'

Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes | Out-Null
Set-TestValue $stl '' '{Another-Application-After-Install}'
$changed = Read-TestValue $stl
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $stl) -ceq $changed) 'Uninstall overwrote a later application change.'
Assert ((Read-TestValue '.stl') -match 'Original.OpenWith.Application') 'Default file association changed.'
Assert ((Read-TestValue $stl 'Unrelated') -match '17') 'Unrelated registry value changed.'
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $stl) -ceq $changed) 'Repeated uninstall changed external registration.'
Write-Output 'PASS conditional restoration and repeated uninstall; default application preserved'

Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes | Out-Null
$state = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
$state.Status = 'Installing' # Simulate a process ending before the final journal update.
$state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $journal -Encoding utf8
$valid = [IO.File]::ReadAllBytes($journal)
$state.Entries[0].SubKey = '..\Unrelated'
$state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $journal -Encoding utf8
Expect-Failure { Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes }
[IO.File]::WriteAllBytes($journal, $valid)
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $stl) -ceq $changed) 'Interrupted install did not recover original registration.'
Write-Output 'PASS interrupted-install recovery and tampered-journal refusal'

Assert (@(Get-ShellRegistrationPlan -DllPath $dll | Where-Object Name -eq 'DisableProcessIsolation').Count -eq 0) 'Default plan weakens Shell isolation.'
# This is only a synthetic test subtree, not a COM registration consumed by Windows.
Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes -AllowInProcessBridge | Out-Null
$status = Get-ShellRegistrationStatus -StatePath $journal -ClassesPath $classes
Assert (@($status.Entries | Where-Object { -not $_.Owned }).Count -eq 0) 'Explicit bridge plan differs.'
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Write-Output 'PASS default isolation preserved and explicit DWORD option round-trip in synthetic test subtree'

$binding = @([pscustomobject]@{Extension='.stl';ProgId='Test.STL.Application'})
$boundPath = 'Test.STL.Application\ShellEx\' + $category
Set-TestValue $boundPath '' '{Original-ProgID-Provider}'
$originalBinding = Read-TestValue $boundPath
Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes -ProgIdBindings $binding | Out-Null
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $boundPath) -ceq $originalBinding) 'Existing ProgID handler was not restored.'
$conflict = @($binding[0], [pscustomobject]@{Extension='.obj';ProgId='Test.STL.Application'})
Expect-Failure { Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes -ProgIdBindings $conflict }
Write-Output 'PASS effective ProgID handler restoration and shared-ProgID conflict refusal'

# This unique test root was created above. Never remove any shared registry tree.
$updatedDll = Join-Path $directory 'updated-shell.dll'
Copy-Item -LiteralPath $dll -Destination $updatedDll
Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes -AllowInProcessBridge | Out-Null
$beforeUpdate = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
Update-ShellRegistration -DllPath $updatedDll -StatePath $journal -ClassesPath $classes | Out-Null
$afterUpdate = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
Assert ($afterUpdate.DllPath -eq $updatedDll) 'Update did not switch runtime.'
Assert ((ConvertTo-Json -InputObject @($beforeUpdate.Entries.Original) -Depth 8 -Compress) -ceq (ConvertTo-Json -InputObject @($afterUpdate.Entries.Original) -Depth 8 -Compress)) 'Update lost original backups.'
Assert (@((Get-ShellRegistrationStatus -StatePath $journal -ClassesPath $classes).Entries | Where-Object {-not $_.Owned}).Count -eq 0) 'Update values differ.'
Set-TestValue $stl '' '{External-After-Update}'
Expect-Failure { Update-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes }
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $stl) -match 'External-After-Update') 'Update/uninstall overwrote external change.'
Write-Output 'PASS update retains original backups, switches runtime, and refuses external changes'

Install-ShellRegistration -DllPath $dll -StatePath $journal -ClassesPath $classes -AllowInProcessBridge | Out-Null
$partial = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
$partial | Add-Member -NotePropertyName PreviousDllPath -NotePropertyValue $dll
$partial.Status = 'Updating'
$partial.DllPath = $updatedDll
$newPlan = @(Get-ShellRegistrationPlan -DllPath $updatedDll -AllowInProcessBridge)
for ($index = 0; $index -lt $partial.Entries.Count; $index++) {
    $entry = $partial.Entries[$index]
    $entry | Add-Member -NotePropertyName PreviousDesired -NotePropertyValue $entry.Desired
    $entry.Desired = $newPlan[$index].Desired
    if ($index % 2 -eq 0) { Set-TestValue $entry.SubKey $entry.Name $entry.Desired -Kind $entry.DesiredKind }
}
$partial | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $journal -Encoding utf8
Uninstall-ShellRegistration -StatePath $journal -ClassesPath $classes | Out-Null
Assert ((Read-TestValue $stl) -match 'External-After-Update') 'Interrupted update did not restore original handler.'
$restored = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
Assert (@($restored.Entries | Where-Object Result -ne 'Restored').Count -eq 0) 'Interrupted update left mixed old/new registrations.'
Write-Output 'PASS interrupted update recovers mixed previous/current values'

Assert ($testRoot -eq ('Software\3DThumbnailTests\' + $identity)) 'Test cleanup scope changed.'
[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testRoot, $false)
Write-Output ('Journal evidence: ' + $directory)
