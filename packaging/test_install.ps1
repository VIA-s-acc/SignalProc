# Usage: test_install.ps1 -Release <dir> [-Tag v9.9.9]
# <dir>/<tag>/ holds signals-<tag>-windows-x86_64.zip and SHA256SUMS (made by package.sh).
# Runs install.ps1 the way `irm ... | iex` does, in a child PowerShell of the same kind.
param(
    [Parameter(Mandatory = $true)][string]$Release,
    [string]$Tag = 'v9.9.9'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installer = Join-Path $root 'install.ps1'
$shell = (Get-Process -Id $PID).Path
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('signals-test-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
$failures = 0

$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
$savedPath = $key.GetValue('Path', $null, 'DoNotExpandEnvironmentNames')
$savedKind = if ($null -ne $savedPath) { $key.GetValueKind('Path') } else { $null }

function Pass([string]$name) { Write-Output "PASS $name" }
function Fail([string]$name, [string]$why, [string]$log) {
    Write-Output "FAIL ${name}: $why"
    if ($log) { $log -split "`n" | ForEach-Object { Write-Output "    | $_" } }
    $script:failures++
}

function Invoke-Install([string]$release, [string]$dir) {
    $env:SIGNALS_RELEASE_URL = 'file:///' + ($release -replace '\\', '/')
    $env:SIGNALS_VERSION = $Tag
    $env:SIGNALS_INSTALL_DIR = $dir
    $command = "Get-Content -Raw -LiteralPath '$installer' | Invoke-Expression"
    $out = & $shell -NoProfile -NonInteractive -Command $command 2>&1 | Out-String
    return @{ Code = $LASTEXITCODE; Out = $out }
}

function Get-UserPathCount([string]$dir) {
    $raw = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment').GetValue('Path', '', 'DoNotExpandEnvironmentNames')
    $want = $dir.TrimEnd('\').ToLowerInvariant()
    return @($raw -split ';' | Where-Object { $_.TrimEnd('\').ToLowerInvariant() -eq $want }).Count
}

try {
    $dir = Join-Path $tmp 'dir with space'
    $r = Invoke-Install $Release $dir
    $exe = Join-Path $dir 'signals.exe'
    if ($r.Code -ne 0) { Fail 'fresh' "exit code $($r.Code)" $r.Out }
    elseif (-not (Test-Path -LiteralPath $exe)) { Fail 'fresh' 'signals.exe was not installed' $r.Out }
    elseif (-not ((& $exe --version) -like 'signals *')) { Fail 'fresh' 'installed signals.exe does not print its version' $r.Out }
    elseif ((Get-UserPathCount $dir) -ne 1) { Fail 'fresh' 'install folder is not in the user PATH once' $r.Out }
    else { Pass 'fresh' }

    $r = Invoke-Install $Release $dir
    if ($r.Code -ne 0) { Fail 'again' "exit code $($r.Code)" $r.Out }
    elseif ($r.Out -notmatch 'already installed') { Fail 'again' "second run does not say 'already installed'" $r.Out }
    elseif ((Get-UserPathCount $dir) -ne 1) { Fail 'again' 'install folder is in the user PATH more than once' $r.Out }
    else { Pass 'again' }

    $bad = Join-Path $tmp 'rel-bad'
    Copy-Item -Recurse -LiteralPath $Release -Destination $bad
    $sums = Join-Path (Join-Path $bad $Tag) 'SHA256SUMS'
    (Get-Content -LiteralPath $sums) -replace '^[0-9a-fA-F]{64}', ('0' * 64) | Set-Content -LiteralPath $sums
    $dir = Join-Path $tmp 'bad'
    $r = Invoke-Install $bad $dir
    if ($r.Code -eq 0) { Fail 'bad-sum' 'exit code is 0' $r.Out }
    elseif ($r.Out -notmatch 'checksum') { Fail 'bad-sum' 'error does not mention the checksum' $r.Out }
    elseif (Test-Path -LiteralPath (Join-Path $dir 'signals.exe')) { Fail 'bad-sum' 'signals.exe was installed anyway' $r.Out }
    else { Pass 'bad-sum' }
}
finally {
    if ($null -eq $savedPath) { $key.DeleteValue('Path', $false) } else { $key.SetValue('Path', $savedPath, $savedKind) }
    Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
}

if ($failures -ne 0) { exit 1 }
