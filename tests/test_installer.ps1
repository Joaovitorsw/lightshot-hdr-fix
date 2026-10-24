# Run with Windows PowerShell 5.1. No Pester, Python or installed application needed.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Backend = Join-Path $PSScriptRoot '..\installer\Install-Core.ps1'
$Root = Join-Path ([IO.Path]::GetTempPath()) ('Lightshot-HDR-tests-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($Root)
$script:Count = 0

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Fixture {
    $case = Join-Path $Root ([guid]::NewGuid().ToString('N'))
    $source = Join-Path $case 'original'
    [void][IO.Directory]::CreateDirectory((Join-Path $source 'resources'))
    [IO.File]::WriteAllBytes((Join-Path $source 'Lightshot.dll'), [byte[]](0, 1, 2, 3, 4, 5, 6, 7))
    [IO.File]::WriteAllText((Join-Path $source 'Lightshot.exe'), 'internal launcher fixture')
    [IO.File]::WriteAllText((Join-Path $source 'DXGIODScreenshot.dll'), 'original capture fixture')
    [IO.File]::WriteAllText((Join-Path $source 'resources\pt.txt'), 'preserve resource')
    $new = Join-Path $case 'capture.dll'
    $pe = New-Object byte[] 256
    $pe[0] = 0x4d; $pe[1] = 0x5a; $pe[0x3c] = 0x80
    $pe[0x80] = 0x50; $pe[0x81] = 0x45
    $pe[0x84] = 0x4c; $pe[0x85] = 1; $pe[0x97] = 0x20
    [IO.File]::WriteAllBytes($new, $pe)
    $hashes = [ordered]@{}
    foreach ($name in @('Lightshot.dll', 'Lightshot.exe', 'DXGIODScreenshot.dll')) {
        $hashes[$name] = Hash (Join-Path $source $name)
    }
    $spec = [ordered]@{schema_version = 1; original_hashes = $hashes
        patches = @(@{offset = 2; original = '0203'; replacement = '9090'})
        new_dll_sha256 = (Hash $new)}
    $specPath = Join-Path $case 'spec.json'
    [IO.File]::WriteAllText($specPath, ($spec | ConvertTo-Json -Depth 10))
    @{Root = $case; Source = $source; Destination = (Join-Path $case 'application');
        NewDll = $new; Spec = $specPath; Contract = $spec; Hashes = $hashes}
}
function Invoke-Backend($Fixture, [string]$Action = 'Install', [string]$Destination = '', [string]$Source = '', [switch]$AutoDetect, [string]$ChildModulePath = '') {
    if (-not $Destination) { $Destination = $Fixture.Destination }
    if (-not $Source) { $Source = $Fixture.Source }
    $report = Join-Path $Fixture.Root ('report-' + [guid]::NewGuid().ToString('N') + '.json')
    $entryPoint = $Backend
    if ($ChildModulePath) {
        $entryPoint = Join-Path $Fixture.Root 'contaminated-host.ps1'
        $wrapper = "Remove-Module Microsoft.PowerShell.Utility -Force -ErrorAction SilentlyContinue`r`n" +
            "`$PSModuleAutoloadingPreference = 'None'`r`n" +
            '$env:PSModulePath = ''' + $ChildModulePath.Replace("'", "''") + "'`r`n& '" + $Backend.Replace("'", "''") + "' @args"
        [IO.File]::WriteAllText($entryPoint, $wrapper)
    }
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $entryPoint,
        '-Action', $Action, '-Destination', $Destination, '-NewDll', $Fixture.NewDll,
        '-Spec', $Fixture.Spec, '-Report', $report)
    if (-not $AutoDetect) { $arguments += @('-Source', $Source) }
    $log = & powershell.exe @arguments 2>&1
    $code = $LASTEXITCODE
    Assert-True (Test-Path -LiteralPath $report) ('No JSON report: ' + ($log -join ' '))
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Assert-True (($code -eq 0) -eq [bool]$result.success) 'Exit code disagrees with JSON success'
    foreach ($suffix in @('.source.txt', '.error.txt')) {
        Assert-True (Test-Path -LiteralPath ($report + $suffix)) 'Missing Inno IPC sidecar'
        $bytes = [IO.File]::ReadAllBytes($report + $suffix)
        Assert-True (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191)) 'IPC sidecar has BOM'
    }
    if ($result.success) {
        Assert-True ([IO.File]::ReadAllText($report + '.source.txt') -eq $result.source) 'IPC source disagrees'
    } else {
        Assert-True ([IO.File]::ReadAllText($report + '.error.txt') -eq $result.error) 'IPC error disagrees'
    }
    $result
}
function Test-Case([string]$Name, [scriptblock]$Body) {
    & $Body
    $script:Count++
    Write-Output ('PASS ' + $Name)
}
try {
    Test-Case 'Wrong original hash refuses without creating destination' {
        $f = Fixture
        [IO.File]::WriteAllText((Join-Path $f.Source 'Lightshot.dll'), 'wrong')
        $r = Invoke-Backend $f
        Assert-True (-not $r.success) 'Wrong hash accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Destination created on validation failure'
        Assert-True ($r.error -match 'hash|SHA') 'Missing helpful hash diagnostic'
    }
    Test-Case 'Patch byte mismatch is independent from hash verification' {
        $f = Fixture
        $f.Contract.patches[0].original = 'ffff'
        [IO.File]::WriteAllText($f.Spec, ($f.Contract | ConvertTo-Json -Depth 10))
        $r = Invoke-Backend $f
        Assert-True (-not $r.success) 'Wrong opcodes accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Destination created for wrong opcodes'
    }
    Test-Case 'New DLL hash and x86 PE checks precede destination creation' {
        $f = Fixture
        [IO.File]::AppendAllText($f.NewDll, 'changed')
        Assert-True (-not (Invoke-Backend $f).success) 'Wrong new DLL hash accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Destination created for wrong new DLL'
        $bytes = [IO.File]::ReadAllBytes($f.NewDll)
        $bytes[0x84] = 0x64; $bytes[0x85] = 0x86
        [IO.File]::WriteAllBytes($f.NewDll, $bytes)
        $f.Contract.new_dll_sha256 = Hash $f.NewDll
        [IO.File]::WriteAllText($f.Spec, ($f.Contract | ConvertTo-Json -Depth 10))
        Assert-True (-not (Invoke-Backend $f).success) 'x64 DLL accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Destination created for x64 DLL'
    }
    Test-Case 'Validate and explicit Detect are read only' {
        $f = Fixture
        Assert-True (Invoke-Backend $f 'Validate').success 'Validate failed'
        Assert-True (Invoke-Backend $f 'Detect').success 'Explicit Detect failed'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Read only action created destination'
    }
    Test-Case 'Windows PowerShell backend works with an invalid inherited PSModulePath' {
        $f = Fixture
        $previous = $env:PSModulePath
        try {
            $env:PSModulePath = Join-Path $f.Root 'nonexistent-modules'
            $r = Invoke-Backend $f 'Validate'
            Assert-True $r.success $r.error
            Assert-True (Invoke-Backend $f).success 'Install failed with invalid inherited module path'
            Assert-True (Invoke-Backend $f 'Uninstall').success 'Uninstall failed with invalid inherited module path'
        } finally { $env:PSModulePath = $previous }
    }
    Test-Case 'A higher priority incompatible Utility module cannot shadow bundled Get-FileHash' {
        $f = Fixture
        $modules = Join-Path $f.Root 'foreign-modules'
        $utility = Join-Path $modules 'Microsoft.PowerShell.Utility'
        [void][IO.Directory]::CreateDirectory($utility)
        $fake = "@{ModuleVersion='7.0.0.0'; CompatiblePSEditions=@('Core'); CmdletsToExport=@('Get-FileHash'); NestedModules=@('Missing.Utility.dll')}"
        [IO.File]::WriteAllText((Join-Path $utility 'Microsoft.PowerShell.Utility.psd1'), $fake)
        $r = Invoke-Backend $f 'Validate' -ChildModulePath $modules
        Assert-True $r.success $r.error
    }
    Test-Case 'Repeated separators normalize in explicit Detect source IPC' {
        $f = Fixture
        $repeated = $f.Source.Replace('\', '\\')
        $r = Invoke-Backend $f 'Detect' '' $repeated
        Assert-True $r.success $r.error
        Assert-True ($r.source -ceq $f.Source) 'Detected source is not canonical'
        Assert-True (-not $r.source.Contains('\\')) 'Repeated separators escaped to source IPC'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Detect mutated destination'
    }
    Test-Case 'Automatic Detect normalizes repeated separators inherited in ProgramFiles' {
        $f = Fixture
        $programFiles = Join-Path $f.Root 'Program Files (x86)'
        $versions = Join-Path $programFiles 'Skillbrains\lightshot'
        [void][IO.Directory]::CreateDirectory($versions)
        $version = Join-Path $versions '5.5.0.7'
        [IO.Directory]::Move($f.Source, $version)
        $f.Source = $version
        $previous = ${env:ProgramFiles(x86)}
        try {
            ${env:ProgramFiles(x86)} = $programFiles.Replace('\', '\\')
            $r = Invoke-Backend $f 'Detect' -AutoDetect
            Assert-True $r.success $r.error
            Assert-True ($r.source -ceq $version) 'Automatic detection source is not canonical'
            Assert-True (-not $r.source.Contains('\\')) 'Repeated environment separators escaped to source IPC'
        } finally { ${env:ProgramFiles(x86)} = $previous }
    }
    Test-Case 'Installation copies resources and backups without touching source' {
        $f = Fixture
        $r = Invoke-Backend $f
        Assert-True $r.success $r.error
        foreach ($name in $f.Hashes.Keys) {
            Assert-True ((Hash (Join-Path $f.Source $name)) -eq $f.Hashes[$name]) 'Original changed'
        }
        Assert-True ((Hash (Join-Path $f.Destination 'backup\Lightshot.dll')) -eq $f.Hashes['Lightshot.dll']) 'Backup missing'
        Assert-True ((Hash (Join-Path $f.Destination 'backup\DXGIODScreenshot.dll')) -eq $f.Hashes['DXGIODScreenshot.dll']) 'Capture backup missing'
        Assert-True ((Hash (Join-Path $f.Destination 'DXGIODScreenshot.dll')) -eq (Hash $f.NewDll)) 'New DLL not installed'
        $bytes = [IO.File]::ReadAllBytes((Join-Path $f.Destination 'Lightshot.dll'))
        Assert-True (($bytes[2] -eq 0x90) -and ($bytes[3] -eq 0x90)) 'Patch missing'
        Assert-True ((Get-Content -LiteralPath (Join-Path $f.Destination 'resources\pt.txt') -Raw) -eq 'preserve resource') 'Resource missing'
        $repeat = Invoke-Backend $f
        Assert-True ($repeat.success -and $repeat.idempotent) 'Exact repeat was not idempotent'
        [IO.File]::WriteAllText((Join-Path $f.Destination 'Lightshot.exe'), 'modified')
        Assert-True (-not (Invoke-Backend $f).success) 'Modified destination accepted'
    }
    Test-Case 'Existing unowned folder and overlap are refused' {
        $f = Fixture
        [void][IO.Directory]::CreateDirectory($f.Destination)
        [IO.File]::WriteAllText((Join-Path $f.Destination 'keep.txt'), 'keep')
        Assert-True (-not (Invoke-Backend $f).success) 'Unowned destination accepted'
        Assert-True (-not (Invoke-Backend $f 'Install' (Join-Path $f.Source 'nested')).success) 'Nested original accepted'
        Assert-True ((Get-Content -LiteralPath (Join-Path $f.Destination 'keep.txt') -Raw) -eq 'keep') 'Unknown file changed'
    }
    Test-Case 'Literal bracket paths work without wildcard expansion' {
        $f = Fixture
        $f.Destination = Join-Path $f.Root 'application [literal]'
        Assert-True (Invoke-Backend $f).success 'Literal path install failed'
        Assert-True (Invoke-Backend $f 'Uninstall').success 'Literal path uninstall failed'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Literal path remains'
    }
    Test-Case 'Junction ancestry and source tree are refused' {
        $f = Fixture
        $link = Join-Path $f.Root 'junction'
        [void](New-Item -ItemType Junction -Path $link -Target $f.Source)
        Assert-True (-not (Invoke-Backend $f 'Install' (Join-Path $link 'copy')).success) 'Destination junction accepted'
        $sourceLink = Join-Path $f.Source 'linked'
        [void](New-Item -ItemType Junction -Path $sourceLink -Target $f.Root)
        Assert-True (-not (Invoke-Backend $f 'Validate').success) 'Source junction accepted'
        [IO.Directory]::Delete($sourceLink)
        [IO.Directory]::Delete($link)
    }
    Test-Case 'Dangling source junction is refused by read-only validation' {
        $f = Fixture
        $target = Join-Path $f.Root 'disappearing-target'
        [void][IO.Directory]::CreateDirectory($target)
        $link = Join-Path $f.Source 'dangling'
        [void](New-Item -ItemType Junction -Path $link -Target $target)
        [IO.Directory]::Delete($target)
        Assert-True (-not (Invoke-Backend $f 'Validate').success) 'Dangling source junction accepted'
        [IO.Directory]::Delete($link)
    }
    Test-Case 'Managed uninstall preserves unknown and modified user files' {
        $f = Fixture
        Assert-True (Invoke-Backend $f).success 'Install failed'
        [IO.File]::WriteAllText((Join-Path $f.Destination 'personal.txt'), 'personal')
        [IO.File]::WriteAllText((Join-Path $f.Destination 'Lightshot.exe'), 'modified by user')
        $r = Invoke-Backend $f 'Uninstall'
        Assert-True $r.success $r.error
        Assert-True (Test-Path -LiteralPath (Join-Path $f.Destination 'personal.txt')) 'Unknown file deleted'
        Assert-True (Test-Path -LiteralPath (Join-Path $f.Destination 'Lightshot.exe')) 'Modified file deleted'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $f.Destination 'DXGIODScreenshot.dll'))) 'Managed file left'
        Assert-True ((Hash (Join-Path $f.Source 'Lightshot.dll')) -eq $f.Hashes['Lightshot.dll']) 'Uninstall changed original'
    }
    Test-Case 'Uninstall refuses an unknown junction before deleting managed files' {
        $f = Fixture
        Assert-True (Invoke-Backend $f).success 'Install failed'
        $link = Join-Path $f.Destination 'linked-user-folder'
        [void](New-Item -ItemType Junction -Path $link -Target $f.Source)
        Assert-True (-not (Invoke-Backend $f 'Uninstall').success) 'Uninstall accepted junction'
        Assert-True (Test-Path -LiteralPath (Join-Path $f.Destination 'Lightshot.dll')) 'Uninstall deleted before rejecting junction'
        [IO.Directory]::Delete($link)
    }
    Test-Case 'Manifest traversal refuses before uninstall deletion' {
        $f = Fixture
        Assert-True (Invoke-Backend $f).success 'Install failed'
        $manifestPath = Join-Path $f.Destination 'HDR-install-manifest.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $manifest.files[0].path = '..\capture.dll'
        [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 20))
        Assert-True (-not (Invoke-Backend $f 'Uninstall').success) 'Traversal manifest accepted'
        Assert-True (Test-Path -LiteralPath (Join-Path $f.Destination 'DXGIODScreenshot.dll')) 'Uninstall partially deleted before validation'
        Assert-True (Test-Path -LiteralPath $f.NewDll) 'External file deleted'
    }
    Test-Case 'Complete managed uninstall removes only its empty application tree' {
        $f = Fixture
        Assert-True (Invoke-Backend $f).success 'Install failed'
        Assert-True (Invoke-Backend $f 'Uninstall').success 'Uninstall failed'
        Assert-True (-not (Test-Path -LiteralPath $f.Destination)) 'Empty managed application tree remains'
        Assert-True (Test-Path -LiteralPath $f.Source) 'Original tree removed'
        Assert-True (-not (Invoke-Backend $f 'Uninstall' $f.Source).success) 'Unowned original accepted for uninstall'
        Assert-True ((Hash (Join-Path $f.Source 'Lightshot.exe')) -eq $f.Hashes['Lightshot.exe']) 'Original EXE changed'
    }
    Write-Output ('Installer tests passed: ' + $script:Count)
} finally {
    # Only the exact generated fixture root is eligible for recursive cleanup.
    $resolvedRoot = [IO.Path]::GetFullPath($Root)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedRoot)).StartsWith('Lightshot-HDR-tests-')) { throw 'Unsafe fixture cleanup' }
    # Remove junction entries without traversing their targets, before recursive cleanup.
    function Clear-FixtureLinks([string]$Path) {
        foreach ($entry in [IO.Directory]::GetFileSystemEntries($Path)) {
            $attributes = [IO.File]::GetAttributes($entry)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) { [IO.Directory]::Delete($entry) }
                else { [IO.File]::Delete($entry) }
            } elseif (($attributes -band [IO.FileAttributes]::Directory) -ne 0) { Clear-FixtureLinks $entry }
        }
    }
    Clear-FixtureLinks $resolvedRoot
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
