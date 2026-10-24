$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\installer\Launch-Fix.ps1')
function Get-ProcessOwnerSid { param($Candidate) return $Candidate.TestOwnerSid }
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('hdr-launcher-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $executable = Join-Path $temporary 'Lightshot.exe'
    [IO.File]::WriteAllBytes($executable, [byte[]](1,2,3,4))
    $hash = (Get-FileHash -LiteralPath $executable).Hash
    $candidate = [PSCustomObject]@{Name='Lightshot.exe';SessionId=7;ExecutablePath=$executable;TestOwnerSid='S-1-5-21-100'}
    if (-not (Test-CompatibleProcess $candidate $hash 7 'S-1-5-21-100')) { throw 'Compatible process was not recognized.' }
    if (Test-CompatibleProcess $candidate $hash 7 'S-1-5-21-200') { throw 'Another owner accepted.' }
    if (Test-CompatibleProcess $candidate ('0'*64) 7 'S-1-5-21-100') { throw 'Wrong binary hash accepted.' }
    if (Test-CompatibleProcess $candidate $hash 8 'S-1-5-21-100') { throw 'Another session accepted.' }
    $candidate.Name = 'Other.exe'
    if (Test-CompatibleProcess $candidate $hash 7 'S-1-5-21-100') { throw 'Another process name accepted.' }
    $candidate.Name = 'Lightshot.exe'; $candidate.ExecutablePath = ''
    if (Test-CompatibleProcess $candidate $hash 7 'S-1-5-21-100') { throw 'Missing image path accepted.' }
    $other = Join-Path $temporary 'OtherLightshot.exe'
    [IO.File]::WriteAllBytes($other, [byte[]](1,2,3,4))
    $specification = Join-Path $temporary 'spec.json'
    [IO.File]::WriteAllText($specification, (@{original_hashes=@{'Lightshot.exe'=$hash}} | ConvertTo-Json))
    $session = (Get-Process -Id $PID).SessionId
    $owner = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $script:Candidates = @()
    $script:StopCalled = $false
    function Get-CimInstance { param($ClassName, $Filter) return $script:Candidates }
    function Stop-Process { param($Id) $script:StopCalled = $true; throw 'Unexpected process mutation.' }
    if ((Invoke-FixLaunch 'Check' $executable $specification $false) -ne 0) { throw 'Empty process check failed.' }
    $script:Candidates = @([PSCustomObject]@{Name='Lightshot.exe';SessionId=$session;
        ExecutablePath=$other;TestOwnerSid=$owner;ProcessId=999999})
    $refused = $false
    try { [void](Invoke-FixLaunch 'Launch' $executable $specification $false) }
    catch {
        $refused = $_.Exception.Message -like 'Close the running Lightshot*'
        if (-not $refused) { throw ('Unexpected activation error: ' + $_.Exception.Message) }
    }
    if (-not $refused -or $script:StopCalled) { throw 'Newly appearing process was not refused without consent.' }
    if ((Invoke-FixLaunch 'Check' $executable $specification $false) -ne 10) { throw 'Compatible running process not reported.' }
    $script:Candidates[0].TestOwnerSid = 'S-1-5-21-other-user'
    if ((Invoke-FixLaunch 'Check' $executable $specification $false) -ne 11) { throw 'Different owner not rejected.' }
    'PASSED: hash, owner/session isolation, name/path guards and no-consent activation race.'
} finally {
    Remove-Item -LiteralPath (Join-Path $temporary 'OtherLightshot.exe') -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $temporary 'spec.json') -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $temporary 'Lightshot.exe') -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
}
