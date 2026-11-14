# Installs Signals from a GitHub release for the current user.
#
#   irm https://raw.githubusercontent.com/VIA-s-acc/SignalProc/main/install.ps1 | iex
#
# Environment: SIGNALS_VERSION (tag, default: latest), SIGNALS_INSTALL_DIR
# (default: %LOCALAPPDATA%\Programs\signals), SIGNALS_RELEASE_URL (where release files live).
#
# Under `irm | iex` this runs inside the caller's session: errors are thrown, never `exit`,
# which would close the user's window.
& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $repo = 'VIA-s-acc/SignalProc'
    $version = $env:SIGNALS_VERSION
    $dir = if ($env:SIGNALS_INSTALL_DIR) { $env:SIGNALS_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\signals' }
    $base = if ($env:SIGNALS_RELEASE_URL) { $env:SIGNALS_RELEASE_URL.TrimEnd('/') } else { "https://github.com/$repo/releases/download" }

    function Get-ReleaseFile([string]$url, [string]$out) {
        if ($url -like 'file://*') { Copy-Item -LiteralPath ([Uri]$url).LocalPath -Destination $out }
        else { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out }
    }

    $cpu = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) { $cpu = $env:PROCESSOR_ARCHITEW6432 }
    if ($cpu -ne 'AMD64' -and $cpu -ne 'ARM64') {
        throw "there is no prebuilt Signals for Windows $cpu; build from source: https://github.com/$repo#build-from-source"
    }

    if (-not $version) {
        # github.com redirects /releases/latest to /releases/tag/<tag>; the REST API would cap a lab behind one address at 60 installs an hour.
        $request = [System.Net.WebRequest]::Create("https://github.com/$repo/releases/latest")
        $request.Method = 'HEAD'
        $response = $request.GetResponse()
        $version = ($response.ResponseUri.AbsoluteUri -split '/tag/')[-1]
        $response.Close()
        if ($version -notmatch '^v[0-9]') { throw "cannot find the latest release of $repo" }
    }

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('signals-' + [Guid]::NewGuid())
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        $sums = Join-Path $tmp 'SHA256SUMS'
        try { Get-ReleaseFile "$base/$version/SHA256SUMS" $sums }
        catch { throw "cannot download SHA256SUMS of ${version}; is the version right?" }

        $file = "signals-$version-windows-x86_64.zip"
        $expected = $null
        foreach ($line in Get-Content -LiteralPath $sums) {
            if ($line -match ('^([0-9a-fA-F]{64})\s+\*?' + [regex]::Escape($file) + '$')) { $expected = $Matches[1] }
        }
        if (-not $expected) { throw "release $version has no Windows build" }

        Write-Output "Downloading $file"
        $zip = Join-Path $tmp $file
        Get-ReleaseFile "$base/$version/$file" $zip
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash -ne $expected) {
            throw "checksum mismatch for $file; nothing was installed"
        }
        Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
        $exe = Join-Path (Join-Path $tmp "signals-$version-windows-x86_64") 'signals.exe'
        if (-not (Test-Path -LiteralPath $exe)) { throw "$file has no signals.exe" }

        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $target = Join-Path $dir 'signals.exe'
        $same = (Test-Path -LiteralPath $target) -and ((Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $exe).Hash)
        $old = ''
        if (-not $same) {
            if (Test-Path -LiteralPath $target) { $old = ((& $target --version 2>$null) | Select-Object -First 1) -replace '^signals ', '' }
            Copy-Item -LiteralPath $exe -Destination $target -Force
        }
        $new = ((& $target --version) | Select-Object -First 1) -replace '^signals ', ''

        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
        $raw = $key.GetValue('Path', '', 'DoNotExpandEnvironmentNames')
        $want = $dir.TrimEnd('\').ToLowerInvariant()
        $entries = @($raw -split ';' | Where-Object { $_ })
        $added = $false
        if (-not ($entries | Where-Object { $_.TrimEnd('\').ToLowerInvariant() -eq $want })) {
            $kind = if ($raw) { $key.GetValueKind('Path') } else { [Microsoft.Win32.RegistryValueKind]::ExpandString }
            $key.SetValue('Path', (($entries + $dir) -join ';'), $kind)
            [Environment]::SetEnvironmentVariable('SIGNALS_PATH_REFRESH', '1', 'User')
            [Environment]::SetEnvironmentVariable('SIGNALS_PATH_REFRESH', $null, 'User')
            $added = $true
        }
        if (-not (($env:Path -split ';') | Where-Object { $_.TrimEnd('\').ToLowerInvariant() -eq $want })) { $env:Path = "$dir;$env:Path" }

        if ($same) { Write-Output "Signals $new is already installed in $dir" }
        elseif ($old -and $old -ne $new) { Write-Output "Signals updated $old -> $new in $dir" }
        else { Write-Output "Signals $new installed to $target" }
        if ($added) { Write-Output "Added $dir to your PATH; new terminals will find 'signals'." }
        Write-Output ''
        Write-Output "Next: run 'signals' to open the interface, or 'signals learn' for the learning path."
        Write-Output 'Windows Terminal shows the interface best: https://aka.ms/terminal'
    }
    finally {
        Remove-Item -Recurse -Force -LiteralPath $tmp -ErrorAction SilentlyContinue
    }
}
