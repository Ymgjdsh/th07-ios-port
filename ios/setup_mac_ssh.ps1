[CmdletBinding()]
param(
    [string]$MacHost = $env:TH07_MAC_HOST,
    [string]$MacUser = $env:TH07_MAC_USER,
    [string]$KeyPath = "$env:USERPROFILE\.ssh\th07_mac"
)

$ErrorActionPreference = "Stop"
$localConfigPath = Join-Path $PSScriptRoot "mac_build.local.psd1"
if (Test-Path -LiteralPath $localConfigPath) {
    $localConfig = Import-PowerShellDataFile -LiteralPath $localConfigPath
    if ([string]::IsNullOrWhiteSpace($MacHost)) { $MacHost = $localConfig.MacHost }
    if ([string]::IsNullOrWhiteSpace($MacUser)) { $MacUser = $localConfig.MacUser }
}
if ([string]::IsNullOrWhiteSpace($MacHost) -or [string]::IsNullOrWhiteSpace($MacUser)) {
    throw "Set MacHost and MacUser in ios\mac_build.local.psd1 before SSH setup."
}

function Test-SshPort {
    param([string]$HostName, [int]$Port = 22)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $pending = $client.ConnectAsync($HostName, $Port)
        if (-not $pending.Wait(3000)) { return $false }
        return $client.Connected
    }
    catch { return $false }
    finally { $client.Dispose() }
}

foreach ($tool in @("ssh", "ssh-keygen")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing Windows OpenSSH tool: $tool"
    }
}

if (-not (Test-SshPort -HostName $MacHost)) {
    throw @"
Cannot reach ${MacHost}:22.
On the Mac, open System Settings > General > Sharing, enable Remote Login,
and allow user '$MacUser'. Then run this script again.
"@
}

$keyDirectory = Split-Path -Parent $KeyPath
New-Item -ItemType Directory -Force -Path $keyDirectory | Out-Null
if (-not (Test-Path -LiteralPath $KeyPath)) {
    # Windows PowerShell 5.1 drops a native empty-string argument. The quoted
    # empty value keeps ssh-keygen's -N argument in the generated command line.
    & ssh-keygen -q -t ed25519 -f $KeyPath -N '""' -C "th07-build@$env:COMPUTERNAME"
    if ($LASTEXITCODE -ne 0) { throw "ssh-keygen failed with exit code $LASTEXITCODE" }
}

$target = "${MacUser}@${MacHost}"
& ssh -i $KeyPath -o BatchMode=yes -o ConnectTimeout=5 `
    -o StrictHostKeyChecking=accept-new $target "printf SSH_KEY_OK"
if ($LASTEXITCODE -eq 0) {
    Write-Host "SSH key is already authorized for $target."
    return
}

$publicKey = (Get-Content -Raw -LiteralPath "${KeyPath}.pub").Trim()
if ($publicKey -notmatch '^ssh-ed25519 [A-Za-z0-9+/=]+ ') {
    throw "Unexpected public key format: ${KeyPath}.pub"
}
$remoteCommand = "umask 077; mkdir -p ~/.ssh; touch ~/.ssh/authorized_keys; " +
    "grep -qxF '$publicKey' ~/.ssh/authorized_keys || printf '%s\n' '$publicKey' >> ~/.ssh/authorized_keys; " +
    "chmod 700 ~/.ssh; chmod 600 ~/.ssh/authorized_keys"

Write-Host "Enter the Mac login password for '$MacUser' once when prompted."
& ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new $target $remoteCommand
if ($LASTEXITCODE -ne 0) { throw "Could not authorize the SSH key." }

& ssh -i $KeyPath -o BatchMode=yes -o ConnectTimeout=5 $target "printf SSH_KEY_OK"
if ($LASTEXITCODE -ne 0) { throw "The key was installed but passwordless login still failed." }

Write-Host "SSH setup complete for $target."
Write-Host "Next: powershell -ExecutionPolicy Bypass -File .\ios\build_on_mac.ps1"
