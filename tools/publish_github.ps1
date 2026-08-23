[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [int]$Build,
    [string]$RepoUrl = "https://github.com/Ymgjdsh/th07-ios-port.git",
    [string]$RemoteName = "backup",
    [string]$Branch = "main",
    [string]$AuthorName = "Ymgjdsh",
    [string]$AuthorEmail = "Ymgjdsh@users.noreply.github.com",
    [switch]$PrepareOnly
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required for source backup."
}
& git rev-parse --is-inside-work-tree | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Not a Git worktree: $root" }
if ($RepoUrl -notmatch '^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?:\.git)?$') {
    throw "RepoUrl must be a credential-free HTTPS GitHub repository URL."
}
if ($RemoteName -notmatch '^[A-Za-z0-9._-]+$' -or $Branch -notmatch '^[A-Za-z0-9._/-]+$') {
    throw "Invalid remote or branch name."
}
if ($AuthorName -match '[\r\n]' -or $AuthorEmail -notmatch '^[A-Za-z0-9._%+-]+@users\.noreply\.github\.com$') {
    throw "Automatic publishing requires a GitHub noreply author identity."
}

$remoteNames = @(& git remote)
if ($LASTEXITCODE -ne 0) { throw "Could not list Git remotes." }
if ($remoteNames -contains $RemoteName) {
    $existingRemote = ((& git remote get-url $RemoteName | Out-String).Trim())
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect Git remote '$RemoteName'." }
    if ($existingRemote -ne $RepoUrl) {
        throw "Remote '$RemoteName' points to '$existingRemote', not '$RepoUrl'."
    }
}
elseif (-not $PrepareOnly) {
    & git remote add $RemoteName $RepoUrl
    if ($LASTEXITCODE -ne 0) { throw "Could not add Git remote '$RemoteName'." }
}
else {
    Write-Host "PrepareOnly: remote '$RemoteName' would be added for $RepoUrl"
}

& git add -A -- .
if ($LASTEXITCODE -ne 0) { throw "Could not stage the source snapshot." }

$stagedFiles = @(& git diff --cached --name-only --diff-filter=ACMR)
if ($LASTEXITCODE -ne 0) { throw "Could not inspect staged files." }
$forbiddenPaths = @($stagedFiles | Where-Object {
    $_ -match '(^|/)(assets|dist|DerivedData|xcuserdata|build[^/]*)/' -or
    $_ -match '(?i)(^|/)(\.env($|\.)|id_(rsa|ed25519)(\.pub)?$|authorized_keys$|credentials?($|\.)|secrets?($|\.)|.*\.local\.(psd1|json|ya?ml|toml)$)' -or
    $_ -match '(?i)\.(ipa|zip|obj|o|pyc|log|pem|key|p12|pfx|mobileprovision|xcuserstate)$'
})
if ($forbiddenPaths.Count -gt 0) {
    throw "Refusing to publish forbidden local, generated, or sensitive paths: $($forbiddenPaths -join ', ')"
}

$oversized = @()
foreach ($file in $stagedFiles) {
    $indexLine = ((& git ls-files -s -- $file | Select-Object -First 1 | Out-String).Trim())
    if ($indexLine -notmatch '^\d+\s+([0-9a-f]+)\s+\d+\s') {
        throw "Could not inspect staged object: $file"
    }
    $objectSizeText = ((& git cat-file -s $Matches[1] | Out-String).Trim())
    if ($objectSizeText -notmatch '^\d+$') { throw "Could not inspect staged object size: $file" }
    $objectSize = [int64]$objectSizeText
    if ($objectSize -gt 50MB) { $oversized += "${file} ($objectSize bytes)" }
}
if ($oversized.Count -gt 0) {
    throw "Refusing to publish files larger than 50 MB: $($oversized -join ', ')"
}

$secretPatterns = @(
    '-----BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
    'github_pat_[A-Za-z0-9_]{20,}',
    'gh[pousr]_[A-Za-z0-9_]{20,}',
    'AKIA[0-9A-Z]{16}',
    'AIza[0-9A-Za-z_-]{30,}',
    'xox[baprs]-[A-Za-z0-9-]{10,}',
    'https?://[^/@[:space:]]+:[^/@[:space:]]+@'
)
$grepArguments = @('grep', '--cached', '-I', '-l', '-E')
foreach ($pattern in $secretPatterns) { $grepArguments += @('-e', $pattern) }
$secretFiles = @(& git @grepArguments -- .)
$grepExit = $LASTEXITCODE
if ($grepExit -eq 0) {
    throw "Potential secret material detected in staged files: $($secretFiles -join ', ')"
}
if ($grepExit -ne 1) { throw "Secret scan failed with exit code $grepExit." }

$privacyLiterals = @($env:USERPROFILE, $env:HOME, $env:COMPUTERNAME)
$localMacConfigPath = Join-Path $root "ios\mac_build.local.psd1"
if (Test-Path -LiteralPath $localMacConfigPath) {
    $localMacConfig = Import-PowerShellDataFile -LiteralPath $localMacConfigPath
    $privacyLiterals += @(
        $localMacConfig.MacHost,
        "/Users/$($localMacConfig.MacUser)",
        "$($localMacConfig.MacUser)@$($localMacConfig.MacHost)"
    )
}
$privacyFiles = @()
foreach ($literal in @($privacyLiterals | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
    $literalMatches = @(& git grep --cached -I -l -F -e $literal -- .)
    $literalExit = $LASTEXITCODE
    if ($literalExit -eq 0) { $privacyFiles += $literalMatches }
    elseif ($literalExit -ne 1) { throw "Privacy scan failed with exit code $literalExit." }
}
if ($privacyFiles.Count -gt 0) {
    throw "Machine-local information detected in staged files: $(($privacyFiles | Select-Object -Unique) -join ', ')"
}

& git diff --cached --check
if ($LASTEXITCODE -ne 0) { throw "Staged source failed Git whitespace checks." }

Write-Host "Security checks passed for $($stagedFiles.Count) staged source files."
Write-Host "Source-only upload list (assets and build products are excluded):"
$stagedFiles | ForEach-Object { Write-Host "  $_" }
if ($PrepareOnly) {
    Write-Host "PrepareOnly: source is staged; no commit or push was performed."
    return
}

$tag = "ios-v${Version}-build${Build}"
$existingTag = ((& git tag --list $tag | Out-String).Trim())
if ($LASTEXITCODE -ne 0) { throw "Could not inspect Git tags." }
if (-not [string]::IsNullOrWhiteSpace($existingTag) -and $stagedFiles.Count -gt 0) {
    throw "Tag '$tag' already exists but the source changed. Increase IosBuild before publishing."
}

if ($stagedFiles.Count -gt 0) {
    & git config --local user.name $AuthorName
    & git config --local user.email $AuthorEmail
    & git commit -m "th07 iOS v${Version} build ${Build}"
    if ($LASTEXITCODE -ne 0) { throw "Git commit failed." }
}
if ([string]::IsNullOrWhiteSpace($existingTag)) {
    & git tag -a $tag -m "th07 iOS v${Version} build ${Build}"
    if ($LASTEXITCODE -ne 0) { throw "Could not create tag '$tag'." }
}

& git push --atomic $RemoteName "HEAD:refs/heads/$Branch" "refs/tags/$tag"
if ($LASTEXITCODE -ne 0) {
    throw "GitHub push failed. Authenticate Git for https://github.com (use Git Credential Manager or a GitHub PAT); no force push was attempted."
}

Write-Host "GitHub source backup complete: $RepoUrl ($Branch, $tag)"
