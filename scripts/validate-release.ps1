[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string] $Tag,

  [Parameter(Mandatory)]
  [string] $Commit,

  [Parameter(Mandatory)]
  [string] $MainRef
)

$ErrorActionPreference = 'Stop'

function Stop-ReleaseValidation {
  param(
    [Parameter(Mandatory)]
    [string] $Message
  )

  Write-Error -Message $Message -ErrorAction Continue
  exit 1
}

function Resolve-GitCommit {
  param(
    [Parameter(Mandatory)]
    [string] $Revision,

    [Parameter(Mandatory)]
    [string] $Description
  )

  $output = @(
    & git rev-parse --verify --end-of-options "$Revision^{commit}" 2>&1
  )
  if ($LASTEXITCODE -ne 0) {
    Stop-ReleaseValidation "Cannot resolve $Description '$Revision' to a Git commit."
  }

  return ($output | Select-Object -Last 1).ToString().Trim()
}

$tagMatch = [regex]::Match(
  $Tag,
  '^v([0-9]+)\.([0-9]+)\.([0-9]+)$'
)
if (-not $tagMatch.Success) {
  Stop-ReleaseValidation `
    "Tag '$Tag' must match ^vMAJOR.MINOR.PATCH$ using decimal components."
}
$tagVersion = '{0}.{1}.{2}' -f `
  $tagMatch.Groups[1].Value,
  $tagMatch.Groups[2].Value,
  $tagMatch.Groups[3].Value

$cmakePath = Join-Path (Get-Location) 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
  Stop-ReleaseValidation "Cannot read root CMake project file '$cmakePath'."
}
try {
  $cmakeSource = Get-Content -LiteralPath $cmakePath -Raw
} catch {
  Stop-ReleaseValidation "Cannot read root CMake project file '$cmakePath': $($_.Exception.Message)"
}

$projectMatch = [regex]::Match(
  $cmakeSource,
  '(?im)^\s*project\s*\(\s*dota_keyboard\s+VERSION\s+([0-9]+)\.([0-9]+)\.([0-9]+)(?=\s|\))'
)
if (-not $projectMatch.Success) {
  Stop-ReleaseValidation `
    'Root CMakeLists.txt must declare project(dota_keyboard VERSION X.Y.Z ...).'
}
$cmakeVersion = '{0}.{1}.{2}' -f `
  $projectMatch.Groups[1].Value,
  $projectMatch.Groups[2].Value,
  $projectMatch.Groups[3].Value

if ($tagVersion -cne $cmakeVersion) {
  Stop-ReleaseValidation `
    "Tag version '$tagVersion' does not match CMake project version '$cmakeVersion'."
}

$resolvedCommit = Resolve-GitCommit -Revision $Commit -Description 'commit'
$resolvedMain = Resolve-GitCommit -Revision $MainRef -Description 'main ref'

$mergeBaseOutput = @(
  & git merge-base --is-ancestor $resolvedCommit $resolvedMain 2>&1
)
if ($LASTEXITCODE -ne 0) {
  Stop-ReleaseValidation `
    "Commit '$Commit' ($resolvedCommit) is not an ancestor of '$MainRef' ($resolvedMain)."
}

Write-Host (
  "Release policy validated: tag '$Tag' matches CMake version '$cmakeVersion'; " +
  "commit '$resolvedCommit' is an ancestor of '$MainRef' ($resolvedMain)."
)
