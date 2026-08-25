[CmdletBinding()]
param(
    [string]$MacHost = $env:TH07_MAC_HOST,
    [string]$MacUser = $env:TH07_MAC_USER,
    [string]$KeyPath = "$env:USERPROFILE\.ssh\th07_mac",
    [string]$RemoteFolder = "th07-build",
    [string]$XcodeApp = "/Applications/Xcode.app",
    [string]$IosVersion = "0.4.0",
    [int]$IosBuild = 35,
    [string]$GitHubRepo = "https://github.com/Ymgjdsh/th07-ios-port.git",
    [switch]$SkipGitHubBackup
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$localConfigPath = Join-Path $PSScriptRoot "mac_build.local.psd1"
if (Test-Path -LiteralPath $localConfigPath) {
    $localConfig = Import-PowerShellDataFile -LiteralPath $localConfigPath
    if ([string]::IsNullOrWhiteSpace($MacHost)) { $MacHost = $localConfig.MacHost }
    if ([string]::IsNullOrWhiteSpace($MacUser)) { $MacUser = $localConfig.MacUser }
}
if ([string]::IsNullOrWhiteSpace($MacHost) -or [string]::IsNullOrWhiteSpace($MacUser)) {
    throw "Set MacHost and MacUser in ios\mac_build.local.psd1 before building."
}
if ($IosVersion -notmatch '^\d+\.\d+\.\d+$' -or $IosBuild -lt 1) {
    throw "IosVersion must be numeric x.y.z and IosBuild must be positive."
}
$target = "${MacUser}@${MacHost}"
$remoteBase = "`$HOME/$RemoteFolder"
$remoteProject = "$remoteBase/th07-ios14-port"
$uploadDirectory = Join-Path $root "dist\remote-upload"
$codeArchive = Join-Path $uploadDirectory "th07-code-build${IosBuild}.zip"
$resultDirectory = Join-Path $root "dist\mac-build"
$logDirectory = Join-Path $resultDirectory "logs-build${IosBuild}"

if ($RemoteFolder -notmatch '^[A-Za-z0-9._/-]+$' -or
    $RemoteFolder.StartsWith("/") -or $RemoteFolder.Contains("..")) {
    throw "RemoteFolder must be a relative path without '..'."
}
if ($XcodeApp -notmatch '^/[A-Za-z0-9._/ -]+$') {
    throw "XcodeApp contains unsupported characters."
}
foreach ($tool in @("ssh", "scp", "python")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing required tool: $tool"
    }
}
$setupScript = Join-Path $PSScriptRoot "setup_mac_ssh.ps1"
if (-not (Test-Path -LiteralPath $setupScript)) {
    throw "Missing SSH setup script: $setupScript"
}
if (-not (Test-Path -LiteralPath $KeyPath)) {
    Write-Host "First run: configuring passwordless Mac login ..."
    & $setupScript -MacHost $MacHost -MacUser $MacUser -KeyPath $KeyPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $KeyPath)) {
        throw "SSH key setup failed."
    }
}

$sshOptions = @(
    "-i", $KeyPath,
    "-o", "BatchMode=yes",
    "-o", "ConnectTimeout=8",
    "-o", "ServerAliveInterval=15",
    "-o", "ServerAliveCountMax=4"
)
$scpOptions = @("-i", $KeyPath, "-o", "BatchMode=yes", "-o", "ConnectTimeout=8")

Write-Host "Checking $target ..."
& ssh @sshOptions $target "printf SSH_OK"
if ($LASTEXITCODE -ne 0) {
    Write-Host "The project key is not authorized on this Mac yet. Running one-time setup ..."
    & $setupScript -MacHost $MacHost -MacUser $MacUser -KeyPath $KeyPath
    if ($LASTEXITCODE -ne 0) { throw "SSH key setup failed." }
    & ssh @sshOptions $target "printf SSH_OK"
    if ($LASTEXITCODE -ne 0) {
        throw "SSH login still failed after key setup."
    }
}

New-Item -ItemType Directory -Force -Path $uploadDirectory, $resultDirectory | Out-Null
Write-Host "Packaging changed source files (large assets are handled separately) ..."
& python (Join-Path $root "ios\package_source.py") --root $root `
    --output $codeArchive --folder "th07-ios14-port" --exclude-assets
if ($LASTEXITCODE -ne 0) { throw "Source packaging failed." }

& ssh @sshOptions $target "mkdir -p $remoteBase/incoming $remoteBase/assets"
if ($LASTEXITCODE -ne 0) { throw "Could not create the remote build directory." }

Write-Host "Uploading source archive ..."
try {
    & scp @scpOptions $codeArchive "${target}:${RemoteFolder}/incoming/source.zip"
    $sourceUploadExit = $LASTEXITCODE
}
finally {
    # This archive is transport-only. Do not leave another source package on
    # the space-constrained Windows machine after the Mac has received it.
    Remove-Item -Force -LiteralPath $codeArchive -ErrorAction SilentlyContinue
}
if ($sourceUploadExit -ne 0) { throw "Source upload failed." }

$assets = @("th07.dat", "thbgm.dat", "msgothic.ttc")
foreach ($asset in $assets) {
    $localAsset = Join-Path $root "assets\$asset"
    if (-not (Test-Path -LiteralPath $localAsset)) { throw "Missing asset: $localAsset" }
    $localHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localAsset).Hash.ToLowerInvariant()
    $remoteHashOutput = & ssh @sshOptions $target `
        "if [ -f $remoteBase/assets/$asset ]; then shasum -a 256 $remoteBase/assets/$asset | cut -d ' ' -f 1; fi"
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect remote asset: $asset" }
    $remoteHash = (($remoteHashOutput | Out-String).Trim())
    if ($remoteHash -ne $localHash) {
        Write-Host "Uploading asset $asset ..."
        & scp @scpOptions $localAsset "${target}:${RemoteFolder}/assets/$asset"
        if ($LASTEXITCODE -ne 0) { throw "Asset upload failed: $asset" }
    }
    else {
        Write-Host "Asset unchanged: $asset"
    }
}

$remotePrepare = @"
set -eu
BASE=$remoteBase
rm -rf "`$BASE/th07-ios14-port"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "`$BASE/incoming/source.zip" "`$BASE"
rm -f "`$BASE/incoming/source.zip"
mkdir -p "`$BASE/th07-ios14-port/assets"
ln -sfn "`$BASE/assets/th07.dat" "`$BASE/th07-ios14-port/assets/th07.dat"
ln -sfn "`$BASE/assets/thbgm.dat" "`$BASE/th07-ios14-port/assets/thbgm.dat"
ln -sfn "`$BASE/assets/msgothic.ttc" "`$BASE/th07-ios14-port/assets/msgothic.ttc"
chmod +x "`$BASE/th07-ios14-port"/ios/*.sh "`$BASE/th07-ios14-port"/ios/*.py
"@ -replace "`r`n", "; " -replace "`n", "; "

Write-Host "Preparing remote project ..."
& ssh @sshOptions $target $remotePrepare
if ($LASTEXITCODE -ne 0) { throw "Remote project preparation failed." }

$remoteIpaName = "th07-ios-${IosVersion}-${IosBuild}.ipa"
$remoteBuild = "export PATH='/usr/local/bin:/opt/homebrew/bin:/Applications/CMake.app/Contents/bin':`$PATH; cd $remoteProject && XCODE_APP='$XcodeApp' IOS_VERSION='$IosVersion' IOS_BUILD='$IosBuild' ./ios/build_ios.sh" +
    (' && desktop_ipa="$HOME/Desktop/' + $remoteIpaName + '" && if [ -e "$desktop_ipa" ] && [ ! -f "$desktop_ipa" ]; then echo "Desktop target exists and is not a file: $desktop_ipa" >&2; exit 1; fi && cp -f "build-ios/' + $remoteIpaName + '" "$desktop_ipa"')
Write-Host "Building on the Mac ..."
& ssh @sshOptions $target $remoteBuild
$buildExit = $LASTEXITCODE

if ($buildExit -ne 0) {
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    & scp @scpOptions "${target}:${RemoteFolder}/th07-ios14-port/build-ios/logs/build-ios.log" $logDirectory 2>$null
    & scp @scpOptions "${target}:${RemoteFolder}/th07-ios14-port/build-ios/logs/last-120-lines.txt" $logDirectory 2>$null
    throw "Remote build failed with exit code $buildExit. Logs were requested into $logDirectory"
}

Write-Host "Checking the built IPA and Mac Desktop copy ..."
$builtCommand = 'shasum -a 256 "$HOME/' + $RemoteFolder + '/th07-ios14-port/build-ios/' + $remoteIpaName + '" | cut -d '' '' -f 1'
$builtHashOutput = & ssh @sshOptions $target $builtCommand
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($builtHashOutput | Out-String).Trim())) {
    throw "IPA build completed, but the build output could not be verified."
}
$desktopCommand = 'shasum -a 256 "$HOME/Desktop/' + $remoteIpaName + '" | cut -d '' '' -f 1'
$desktopHashOutput = & ssh @sshOptions $target `
    $desktopCommand
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($desktopHashOutput | Out-String).Trim())) {
    throw "IPA was built, but the Mac Desktop copy could not be verified."
}

$ipaHash = (($builtHashOutput | Out-String).Trim()).ToLowerInvariant()
$desktopHash = (($desktopHashOutput | Out-String).Trim()).ToLowerInvariant()
if ($desktopHash -ne $ipaHash) {
    throw "Mac Desktop IPA hash differs from the verified build output."
}
if (-not $SkipGitHubBackup) {
    $publishScript = Join-Path $root "tools\publish_github.ps1"
    if (-not (Test-Path -LiteralPath $publishScript)) {
        throw "IPA was built, but the GitHub backup script is missing: $publishScript"
    }
    Write-Host "Publishing the verified source snapshot to GitHub ..."
    & $publishScript -Version $IosVersion -Build $IosBuild -RepoUrl $GitHubRepo
    if ($LASTEXITCODE -ne 0) {
        throw "IPA was built, but the GitHub source backup failed. Mac Desktop: $remoteIpaName"
    }
}
Write-Host ""
Write-Host "SUCCESS: Mac Desktop/$remoteIpaName"
Write-Host "SHA256: $ipaHash"
Write-Host "Mac Desktop: $remoteIpaName (SHA256 $desktopHash)"
