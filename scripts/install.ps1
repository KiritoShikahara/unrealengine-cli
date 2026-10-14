# Installs unrealengine-cli from GitHub Releases after verifying SHA256SUMS.
# $env:UNREALENGINE_CLI_VERSION = 'v0.1.0' pins a release; $env:UNREALENGINE_CLI_REPO overrides owner/name.
$ErrorActionPreference = 'Stop'
$repo = if ($env:UNREALENGINE_CLI_REPO) { $env:UNREALENGINE_CLI_REPO } else { 'KiritoShikahara/unrealengine-cli' }
$version = if ($env:UNREALENGINE_CLI_VERSION) { $env:UNREALENGINE_CLI_VERSION } else { 'latest' }
$asset = 'unrealengine-cli-x86_64-pc-windows-msvc.zip'
$base = if ($version -eq 'latest') { "https://github.com/$repo/releases/latest/download" } else { "https://github.com/$repo/releases/download/$version" }
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("unrealengine-cli-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Invoke-WebRequest "$base/SHA256SUMS" -OutFile "$tmp\SHA256SUMS" -UseBasicParsing
    Invoke-WebRequest "$base/$asset" -OutFile "$tmp\$asset" -UseBasicParsing
    $line = Get-Content "$tmp\SHA256SUMS" | Where-Object { $_ -match " $([regex]::Escape($asset))$" }
    $expected = if ($line) { ($line -split '\s+')[0].ToLower() } else { '' }
    $actual = (Get-FileHash "$tmp\$asset" -Algorithm SHA256).Hash.ToLower()
    if (-not $expected -or $expected -ne $actual) {
        throw "checksum mismatch for $asset; nothing was installed"
    }
    # INSTALL-STEP
    $dest = Join-Path $env:LOCALAPPDATA 'unrealengine-cli\bin'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Expand-Archive "$tmp\$asset" -DestinationPath $tmp -Force
    Copy-Item "$tmp\unrealengine-cli.exe" (Join-Path $dest 'unrealengine-cli.exe') -Force
    $path = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($path -split ';') -notcontains $dest) {
        [Environment]::SetEnvironmentVariable('Path', "$path;$dest", 'User')
    }
    Write-Host "installed: $dest\unrealengine-cli.exe (open a new terminal to refresh PATH)"
} finally {
    Remove-Item -Recurse -Force $tmp
}
