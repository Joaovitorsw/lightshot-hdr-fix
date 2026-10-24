[CmdletBinding()]
param(
    [ValidateSet('Check', 'Launch', 'Interactive', 'StopCopy')][string]$Mode = 'Check',
    [string]$AppExe,
    [string]$Spec,
    [switch]$CloseCompatible
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    foreach ($moduleName in @('Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Management', 'CimCmdlets')) {
        Import-Module ([IO.Path]::Combine($PSHOME, 'Modules', $moduleName, ($moduleName + '.psd1'))) -Force
    }
}

function Get-ProcessOwnerSid {
    param([object]$Candidate)
    $owner = Invoke-CimMethod -InputObject $Candidate -MethodName GetOwnerSid -ErrorAction Stop
    if ($owner.ReturnValue -ne 0) { throw 'Could not verify process ownership.' }
    return $owner.Sid
}

function Test-CompatibleProcess {
    param([object]$Candidate, [string]$ExpectedHash, [int]$SessionId, [string]$OwnerSid)
    if ($Candidate.Name -ne 'Lightshot.exe' -or $Candidate.SessionId -ne $SessionId -or
        [string]::IsNullOrWhiteSpace($Candidate.ExecutablePath)) { return $false }
    try {
        if ((Get-ProcessOwnerSid $Candidate) -ne $OwnerSid) { return $false }
        return (Get-FileHash -LiteralPath $Candidate.ExecutablePath -Algorithm SHA256).Hash -eq $ExpectedHash
    } catch { return $false }
}

function Invoke-FixLaunch {
    param([string]$Operation, [string]$Executable, [string]$Specification, [bool]$AllowClose)
    $configuration = Get-Content -LiteralPath $Specification -Raw | ConvertFrom-Json
    $expected = $configuration.original_hashes.'Lightshot.exe'
    if ($expected -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid executable hash contract.' }
    $target = [IO.Path]::GetFullPath($Executable)
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) {
        throw 'Installed Lightshot executable does not match the supported version.'
    }
    $session = (Get-Process -Id $PID).SessionId
    $ownerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $candidates = @(Get-CimInstance Win32_Process -Filter "Name='Lightshot.exe'" |
        Where-Object { $_.SessionId -eq $session })
    $other = @($candidates | Where-Object { $_.ExecutablePath -ne $target })
    if ($Operation -eq 'Check') {
        foreach ($candidate in $other) {
            if (-not (Test-CompatibleProcess $candidate $expected $session $ownerSid)) { return 11 }
        }
        if ($other.Count -gt 0) { return 10 }
        return 0
    }
    $toClose = @()
    if ($Operation -eq 'StopCopy') {
        $toClose = @($candidates | Where-Object { $_.ExecutablePath -eq $target })
    } else { $toClose = @($other) }
    if ($toClose.Count -gt 0 -and -not $AllowClose) { throw 'Close the running Lightshot before activating this copy.' }
    foreach ($candidate in $toClose) {
        if (-not (Test-CompatibleProcess $candidate $expected $session $ownerSid)) {
            throw 'A different Lightshot version is running. Close it manually first.'
        }
        $process = Get-Process -Id $candidate.ProcessId -ErrorAction Stop
        if ($process.Path -ne $candidate.ExecutablePath -or $process.SessionId -ne $session) {
            throw 'The process changed while preparing activation. Try again.'
        }
        Stop-Process -Id $process.Id -ErrorAction Stop
        if (-not $process.WaitForExit(5000)) { throw 'The previous Lightshot did not exit.' }
    }
    if ($Operation -eq 'Launch' -and -not ($candidates | Where-Object { $_.ExecutablePath -eq $target })) {
        Start-Process -FilePath $target -WorkingDirectory (Split-Path -Parent $target)
    }
    return 0
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        if ($Mode -eq 'Interactive') {
            Add-Type -AssemblyName System.Windows.Forms
            $status = Invoke-FixLaunch 'Check' $AppExe $Spec $false
            if ($status -eq 11) { throw 'Another Lightshot version or owner is active. Close it manually and try again.' }
            $allowClose = $false
            if ($status -eq 10) {
                $message = 'Close your running Lightshot and open the HDR-corrected copy? An active screenshot selection will be cancelled.'
                if ([Globalization.CultureInfo]::CurrentUICulture.Name.StartsWith('pt')) {
                    $message = 'Fechar seu Lightshot atual e abrir a copia corrigida para HDR? Uma selecao de print ativa sera cancelada.'
                }
                $answer = [Windows.Forms.MessageBox]::Show($message, 'Lightshot HDR Fix',
                    [Windows.Forms.MessageBoxButtons]::YesNo, [Windows.Forms.MessageBoxIcon]::Question)
                if ($answer -ne [Windows.Forms.DialogResult]::Yes) { exit 0 }
                $allowClose = $true
            }
            # Re-read processes after consent. A newly appearing process cannot
            # be closed if the earlier check did not obtain explicit approval.
            exit (Invoke-FixLaunch 'Launch' $AppExe $Spec $allowClose)
        }
        exit (Invoke-FixLaunch $Mode $AppExe $Spec ([bool]$CloseCompatible))
    } catch {
        if ($Mode -eq 'Interactive') {
            [void][Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Lightshot HDR Fix')
        } else { Write-Error $_ }
        exit 1
    }
}
