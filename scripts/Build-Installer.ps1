[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$DllPath,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$CompilerPath,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$payload = Join-Path $repository 'build\installer-payload'
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $CompilerPath) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    $CompilerPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath)) {
    throw 'Inno Setup 6 compiler not found. Install it or pass -CompilerPath.'
}
New-Item -ItemType Directory -Path $payload -Force | Out-Null
New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $payload 'DXGIODScreenshot.dll') -Force
& $Python (Join-Path $PSScriptRoot 'export_installer_spec.py') --dll $dll --output (Join-Path $payload 'patch-spec.json')
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the canonical installer patch specification.' }
& $CompilerPath ('/DPayloadDir=' + $payload) ('/DInstallerOutput=' + $output) (Join-Path $repository 'installer\Lightshot-HDR-Fix.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$setup = Join-Path $output 'Lightshot-HDR-Fix-Setup.exe'
Get-FileHash -LiteralPath $setup -Algorithm SHA256
