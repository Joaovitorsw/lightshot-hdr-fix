#requires -Version 5.1
<# File-only installer. No elevation, process changes, registry writes or launch.
   Spec is bundled trusted build metadata generated from package_copy.py.
   Inputs and destination must not be modified concurrently with this script.
   Validate/Detect do not write application files; -Report is an explicit output.
#>
[CmdletBinding()]
param(
    [ValidateSet('Detect', 'Validate', 'Install', 'Uninstall')][string]$Action = 'Detect',
    [string]$Source,
    [string]$Destination,
    [string]$NewDll,
    [string]$Spec,
    [string]$Report
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
# Inno can inherit PowerShell 7 module search paths while launching Desktop 5.1.
# Import this executable's bundled modules explicitly, before using any of their
# commands (including New-Object, JSON conversion and the Get-FileHash function).
foreach ($moduleName in @('Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Management')) {
    $moduleManifest = [IO.Path]::Combine($PSHOME, 'Modules', $moduleName, ($moduleName + '.psd1'))
    Import-Module -Name $moduleManifest -Force -ErrorAction Stop
}
$script:ManifestName = 'HDR-install-manifest.json'
$script:Product = 'Lightshot-HDR-file-install-v1'
$script:Utf8 = New-Object Text.UTF8Encoding($false)

function Get-SafePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathRooted($Path)) {
        throw 'An absolute filesystem path is required.'
    }
    if ($Path.StartsWith('\\') -or $Path -notmatch '^[a-zA-Z]:[\\/]') {
        throw 'Only local drive paths are supported.'
    }
    foreach ($part in ($Path.Substring(3) -split '[\\/]')) {
        if (-not $part) { continue }
        if ($part -in @('.', '..') -or $part -ne $part.TrimEnd('.', ' ') -or
            $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
            $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') {
            throw ('Unsafe path segment: ' + $part)
        }
    }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($full.Length -eq 2) { $full += '\' }
    $current = $full
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $attributes = [IO.File]::GetAttributes($current)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('Reparse/junction/symbolic link refused: ' + $current)
            }
        }
        $parent = [IO.Directory]::GetParent($current)
        if ($null -eq $parent) { break }
        $current = $parent.FullName
    }
    return $full
}

function Test-InTree([string]$Path, [string]$Root) {
    return ($Path.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or
        $Path.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))
}

function Get-ChildPath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative)) {
        throw 'Manifest/spec file paths must be relative.'
    }
    $child = Get-SafePath ([IO.Path]::Combine($Root, $Relative))
    if ($child -eq $Root -or -not (Test-InTree $child $Root)) { throw 'Path traversal refused.' }
    return $child
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-Tree([string]$Root) {
    $rootPath = Get-SafePath $Root
    if (-not [IO.Directory]::Exists($rootPath)) { throw ('Directory not found: ' + $rootPath) }
    $files = New-Object 'Collections.Generic.List[string]'
    $directories = New-Object 'Collections.Generic.List[string]'
    $queue = New-Object 'Collections.Generic.Queue[string]'
    $queue.Enqueue($rootPath)
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        foreach ($entry in [IO.Directory]::GetFileSystemEntries($current)) {
            $safe = Get-SafePath $entry
            if ([IO.Directory]::Exists($safe)) {
                $directories.Add($safe)
                $queue.Enqueue($safe)
            } else { $files.Add($safe) }
        }
    }
    return @{Files = $files.ToArray(); Directories = $directories.ToArray()}
}

function Get-HexBytes([string]$Hex) {
    if (-not $Hex -or $Hex -notmatch '^(?:[a-fA-F0-9]{2})+$') { throw 'Invalid patch hex bytes.' }
    $bytes = New-Object byte[] ($Hex.Length / 2)
    for ($index = 0; $index -lt $bytes.Length; $index++) {
        $bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
    }
    return ,$bytes
}

function Read-Spec([string]$Path) {
    $safe = Get-SafePath $Path
    $contract = [IO.File]::ReadAllText($safe) | ConvertFrom-Json
    if ($contract.schema_version -ne 1 -or $contract.new_dll_sha256 -notmatch '^[a-fA-F0-9]{64}$') {
        throw 'Unsupported or malformed installer spec.'
    }
    $names = @($contract.original_hashes.PSObject.Properties.Name)
    $required = @('Lightshot.dll', 'Lightshot.exe', 'DXGIODScreenshot.dll')
    if (@(Compare-Object $names $required).Count -ne 0) { throw 'Spec must identify exactly the three application binaries.' }
    foreach ($property in $contract.original_hashes.PSObject.Properties) {
        if ($property.Value -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid original SHA-256 in spec.' }
    }
    if (@($contract.patches).Count -eq 0) { throw 'Spec must contain patches.' }
    return $contract
}

function Get-PatchedBytes([byte[]]$Bytes, $Contract) {
    $result = $Bytes.Clone()
    $occupied = New-Object 'Collections.Generic.HashSet[long]'
    foreach ($change in $Contract.patches) {
        if ([string]$change.offset -notmatch '^\d+$') { throw 'Invalid patch offset.' }
        $offset = [long]$change.offset
        $before = Get-HexBytes $change.original
        $after = Get-HexBytes $change.replacement
        if ($before.Length -ne $after.Length -or $offset -gt ($Bytes.Length - $before.Length)) {
            throw 'Patch byte range/length is invalid.'
        }
        for ($index = 0; $index -lt $before.Length; $index++) {
            if (-not $occupied.Add($offset + $index)) { throw 'Overlapping patch ranges refused.' }
            if ($Bytes[$offset + $index] -ne $before[$index]) { throw ('Original patch bytes mismatch at ' + $offset) }
            $result[$offset + $index] = $after[$index]
        }
    }
    return ,$result
}

function Assert-NewDll([string]$Path, $Contract) {
    $safe = Get-SafePath $Path
    if ((Get-Hash $safe) -ne $Contract.new_dll_sha256) { throw 'New capture DLL SHA-256 mismatch.' }
    $bytes = [IO.File]::ReadAllBytes($safe)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'New capture DLL is not a PE file.' }
    $header = [BitConverter]::ToUInt32($bytes, 0x3c)
    if ($header -gt ($bytes.Length - 24) -or [BitConverter]::ToUInt32($bytes, $header) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $header + 4) -ne 0x14c -or
        ([BitConverter]::ToUInt16($bytes, $header + 22) -band 0x2000) -eq 0) {
        throw 'New capture DLL must be an x86 PE DLL.'
    }
}

function Assert-Source([string]$Path, $Contract) {
    $safe = Get-SafePath $Path
    if (-not [IO.Directory]::Exists($safe)) { throw ('Source directory not found: ' + $safe) }
    foreach ($property in $Contract.original_hashes.PSObject.Properties) {
        $file = Get-ChildPath $safe $property.Name
        if ((Get-Hash $file) -ne $property.Value) { throw ('Unsupported original SHA-256: ' + $property.Name) }
    }
    foreach ($reserved in @('backup', $script:ManifestName)) {
        if (Test-Path -LiteralPath (Join-Path $safe $reserved)) { throw ('Source contains reserved output: ' + $reserved) }
    }
    # Reject nonmatching detection candidates before walking their resources.
    $tree = Get-Tree $safe
    [void](Get-PatchedBytes ([IO.File]::ReadAllBytes((Join-Path $safe 'Lightshot.dll'))) $Contract)
    return $tree
}

function Find-Source($Contract, [string]$Explicit) {
    if ($Explicit) {
        $safe = Get-SafePath $Explicit
        [void](Assert-Source $safe $Contract)
        return $safe
    }
    $roots = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($programRoot in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($programRoot) { [void]$roots.Add((Join-Path $programRoot 'Skillbrains\lightshot')) }
    }
    foreach ($registryRoot in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')) {
        if (-not (Test-Path -LiteralPath $registryRoot)) { continue }
        foreach ($key in @(Get-ChildItem -LiteralPath $registryRoot -ErrorAction SilentlyContinue | Select-Object -First 1024)) {
            $entry = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
            if ($null -ne $entry -and $entry.PSObject.Properties['DisplayName'] -and
                $entry.DisplayName -match 'Lightshot' -and $entry.PSObject.Properties['InstallLocation'] -and $entry.InstallLocation) {
                [void]$roots.Add([string]$entry.InstallLocation)
            }
        }
    }
    foreach ($root in $roots) {
        try {
            $safeRoot = Get-SafePath $root
            if (-not [IO.Directory]::Exists($safeRoot)) { continue }
            $candidates = @($safeRoot)
            $candidates += @([IO.Directory]::GetDirectories($safeRoot) | Select-Object -First 32)
            foreach ($candidate in $candidates) {
                try { [void](Assert-Source $candidate $Contract); return (Get-SafePath $candidate) }
                catch { continue }
            }
        } catch { continue }
    }
    throw 'Supported original Lightshot not detected. Select the internal version folder (for example 5.5.0.7).'
}

function Assert-Separate([string]$Original, [string]$Target) {
    if ((Test-InTree $Target $Original) -or (Test-InTree $Original $Target)) {
        throw 'Original and destination must be separate directory trees.'
    }
}

function Read-OwnedManifest([string]$Target) {
    [void](Get-Tree $Target)
    $manifestFile = Get-ChildPath $Target $script:ManifestName
    if (-not [IO.File]::Exists($manifestFile)) { throw 'Existing destination has no owned installation manifest.' }
    $manifest = [IO.File]::ReadAllText($manifestFile) | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or $manifest.product -ne $script:Product -or
        -not $Target.Equals([string]$manifest.destination, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Destination manifest does not identify this installation.'
    }
    $origin = Get-SafePath $manifest.source
    Assert-Separate $origin $Target
    $seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $manifest.files) {
        [void](Get-ChildPath $Target $entry.path)
        if ($entry.path -eq $script:ManifestName -or -not $seen.Add($entry.path) -or
            $entry.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid or duplicate managed manifest file.' }
    }
    foreach ($required in @('Lightshot.exe', 'Lightshot.dll', 'DXGIODScreenshot.dll', 'backup\Lightshot.dll', 'backup\DXGIODScreenshot.dll')) {
        if (-not $seen.Contains($required)) { throw ('Incomplete owned manifest: ' + $required) }
    }
    foreach ($directory in $manifest.directories) { [void](Get-ChildPath $Target $directory) }
    return $manifest
}

function Install-Copy([string]$Original, [string]$Target, [string]$Capture, $Contract, [string]$SpecHash) {
    Assert-Separate $Original $Target
    $tree = Assert-Source $Original $Contract
    Assert-NewDll $Capture $Contract
    if (Test-Path -LiteralPath $Target) {
        $existing = Read-OwnedManifest $Target
        if ($existing.spec_sha256 -ne $SpecHash -or $existing.new_dll_sha256 -ne $Contract.new_dll_sha256) {
            throw 'Existing installation belongs to a different payload/spec; no files overwritten.'
        }
        foreach ($entry in $existing.files) {
            $path = Get-ChildPath $Target $entry.path
            if (-not [IO.File]::Exists($path) -or (Get-Hash $path) -ne $entry.sha256) {
                throw ('Existing installation changed: ' + $entry.path)
            }
        }
        return @{idempotent = $true; manifest = (Join-Path $Target $script:ManifestName)}
    }
    $parent = [IO.Directory]::GetParent($Target)
    if ($null -eq $parent -or -not $parent.Exists) { throw 'Destination parent must already exist.' }
    $originalDll = [IO.File]::ReadAllBytes((Join-Path $Original 'Lightshot.dll'))
    $patched = Get-PatchedBytes $originalDll $Contract
    # Directory creation has no merge/overwrite mode. Partial I/O failures are retained.
    [void](Get-SafePath $Target)
    [void](New-Item -ItemType Directory -Path $Target)
    $directories = New-Object 'Collections.Generic.List[string]'
    foreach ($directory in $tree.Directories) {
        $relative = $directory.Substring($Original.Length + 1)
        [void](New-Item -ItemType Directory -Path (Get-ChildPath $Target $relative))
        $directories.Add($relative)
    }
    foreach ($file in $tree.Files) {
        $relative = $file.Substring($Original.Length + 1)
        [IO.File]::Copy((Get-SafePath $file), (Get-ChildPath $Target $relative), $false)
    }
    foreach ($property in $Contract.original_hashes.PSObject.Properties) {
        if ((Get-Hash (Get-ChildPath $Target $property.Name)) -ne $property.Value) { throw 'Source changed during copy.' }
    }
    $backup = Get-ChildPath $Target 'backup'
    [void](New-Item -ItemType Directory -Path $backup)
    $directories.Add('backup')
    foreach ($name in @('Lightshot.dll', 'DXGIODScreenshot.dll')) {
        [IO.File]::Copy((Get-ChildPath $Target $name), (Get-ChildPath $backup $name), $false)
    }
    [IO.File]::WriteAllBytes((Get-ChildPath $Target 'Lightshot.dll'), $patched)
    [IO.File]::Copy((Get-SafePath $Capture), (Get-ChildPath $Target 'DXGIODScreenshot.dll'), $true)
    Assert-NewDll (Get-ChildPath $Target 'DXGIODScreenshot.dll') $Contract
    $managed = @((Get-Tree $Target).Files | ForEach-Object {
        @{path = $_.Substring($Target.Length + 1); sha256 = (Get-Hash $_)}
    })
    $manifest = [ordered]@{schema_version = 1; product = $script:Product; source = $Original; destination = $Target
        created_utc = [DateTime]::UtcNow.ToString('o'); spec_sha256 = $SpecHash
        new_dll_sha256 = $Contract.new_dll_sha256; original_hashes = $Contract.original_hashes
        patches = $Contract.patches; files = $managed; directories = $directories.ToArray()
        isolation_limitations = 'Application files only; registry, user preferences and mutex may be shared.'}
    $manifestFile = Get-ChildPath $Target $script:ManifestName
    [IO.File]::WriteAllText($manifestFile, ($manifest | ConvertTo-Json -Depth 20), $script:Utf8)
    return @{idempotent = $false; manifest = $manifestFile}
}

function Uninstall-Copy([string]$Target) {
    $manifest = Read-OwnedManifest $Target
    $manifestFile = Get-ChildPath $Target $script:ManifestName
    $manifestHash = Get-Hash $manifestFile
    $removed = New-Object 'Collections.Generic.List[string]'
    $preserved = New-Object 'Collections.Generic.List[string]'
    # Validate the entire tree and every manifest path before the first deletion.
    foreach ($entry in $manifest.files) {
        $path = Get-ChildPath $Target $entry.path
        if (-not [IO.File]::Exists($path)) { continue }
        if ((Get-Hash $path) -ne $entry.sha256) { $preserved.Add($entry.path); continue }
        [void](Get-SafePath $path)
        [IO.File]::Delete($path)
        $removed.Add($entry.path)
    }
    foreach ($relative in @($manifest.directories | Sort-Object Length -Descending)) {
        $directory = Get-ChildPath $Target $relative
        if ([IO.Directory]::Exists($directory) -and [IO.Directory]::GetFileSystemEntries($directory).Length -eq 0) {
            [IO.Directory]::Delete($directory, $false)
        }
    }
    foreach ($file in (Get-Tree $Target).Files) {
        if ($file -ne $manifestFile) {
            $relative = $file.Substring($Target.Length + 1)
            if (-not $preserved.Contains($relative)) { $preserved.Add($relative) }
        }
    }
    if ($preserved.Count -eq 0 -and (Get-Hash $manifestFile) -eq $manifestHash) { [IO.File]::Delete($manifestFile) }
    if ([IO.Directory]::GetFileSystemEntries($Target).Length -eq 0) { [IO.Directory]::Delete($Target, $false) }
    return @{source = $manifest.source; removed = $removed.ToArray(); preserved = $preserved.ToArray()}
}

function Assert-ReportTargets {
    if ($Report) {
        foreach ($reportFile in @($Report, ($Report + '.source.txt'), ($Report + '.error.txt'))) {
            $output = Get-SafePath $reportFile
            if (-not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($output))) { throw 'Report parent must exist.' }
            foreach ($protected in @($Source, $Destination)) {
                # Invalid input paths must still allow a diagnostic report elsewhere.
                if ($protected -and (Test-InTree $output ([IO.Path]::GetFullPath($protected).TrimEnd('\')))) {
                    throw 'Report must be outside source and destination.'
                }
            }
            foreach ($inputFile in @($Spec, $NewDll)) {
                if ($inputFile -and $output -eq [IO.Path]::GetFullPath($inputFile)) { throw 'Report cannot overwrite an input.' }
            }
        }
    }
}

function Write-Result($Result) {
    $json = $Result | ConvertTo-Json -Depth 20
    if ($Report) {
        Assert-ReportTargets
        [IO.File]::WriteAllText((Get-SafePath $Report), $json, $script:Utf8)
        $sourceText = ''; $errorText = ''
        if ($Result.success) { $sourceText = [string]$Result.source }
        else { $errorText = [string]$Result.error }
        [IO.File]::WriteAllText((Get-SafePath ($Report + '.source.txt')), $sourceText, $script:Utf8)
        [IO.File]::WriteAllText((Get-SafePath ($Report + '.error.txt')), $errorText, $script:Utf8)
    }
    Write-Output $json
}

$result = [ordered]@{success = $false; action = $Action; source = $Source; destination = $Destination; error = $null}
$exitCode = 1
try {
    if ($Destination) { $Destination = Get-SafePath $Destination; $result.destination = $Destination }
    # Preflight all three explicit IPC outputs before application mutation.
    Assert-ReportTargets
    if ($Action -eq 'Uninstall') {
        if (-not $Destination) { throw 'Uninstall requires -Destination.' }
        $ownership = Read-OwnedManifest $Destination
        $Source = [string]$ownership.source
        $result.source = $Source
        Assert-ReportTargets
        $details = Uninstall-Copy $Destination
    } else {
        if (-not $Spec) { throw 'Detect/Validate/Install require -Spec.' }
        $contract = Read-Spec $Spec
        $Source = Find-Source $contract $Source
        $result.source = $Source
        Assert-ReportTargets
        if ($Destination) { Assert-Separate $Source $Destination }
        if ($Action -eq 'Install' -or $Action -eq 'Validate') { Assert-NewDll $NewDll $contract }
        $details = @{}
        if ($Action -eq 'Install') {
            if (-not $Destination) { throw 'Install requires -Destination.' }
            $details = Install-Copy $Source $Destination (Get-SafePath $NewDll) $contract (Get-Hash (Get-SafePath $Spec))
        }
    }
    foreach ($key in $details.Keys) { $result[$key] = $details[$key] }
    $result.success = $true
    $exitCode = 0
} catch { $result.error = $_.Exception.Message }
try { Write-Result $result }
catch {
    $result.success = $false
    $result.error = 'Report output failed: ' + $_.Exception.Message
    Write-Output ($result | ConvertTo-Json -Depth 20)
    $exitCode = 1
}
exit $exitCode
