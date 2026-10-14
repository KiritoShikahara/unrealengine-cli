# Installs unreal-cli from GitHub Releases after verifying SHA256SUMS.
# $env:UNREAL_CLI_VERSION = 'v0.1.0' pins a release; $env:UNREAL_CLI_REPO overrides owner/name.
$ErrorActionPreference = 'Stop'
$repo = if ($env:UNREAL_CLI_REPO) { $env:UNREAL_CLI_REPO } else { 'KiritoShikahara/unreal-cli' }
$version = if ($env:UNREAL_CLI_VERSION) { $env:UNREAL_CLI_VERSION } else { 'latest' }
$asset = 'unreal-cli-x86_64-pc-windows-msvc.zip'
$base = if ($version -eq 'latest') { "https://github.com/$repo/releases/latest/download" } else { "https://github.com/$repo/releases/download/$version" }
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("unreal-cli-" + [guid]::NewGuid())
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
    $dest = Join-Path $env:LOCALAPPDATA 'unreal-cli\bin'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Expand-Archive "$tmp\$asset" -DestinationPath $tmp -Force
    Copy-Item "$tmp\unreal-cli.exe" (Join-Path $dest 'unreal-cli.exe') -Force
    $path = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($path -split ';') -notcontains $dest) {
        [Environment]::SetEnvironmentVariable('Path', "$path;$dest", 'User')
    }
    Write-Host "installed: $dest\unreal-cli.exe (open a new terminal to refresh PATH)"
} finally {
    Remove-Item -Recurse -Force $tmp
}
