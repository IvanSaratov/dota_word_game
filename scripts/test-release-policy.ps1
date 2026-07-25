[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$validatorPath = Join-Path $PSScriptRoot 'validate-release.ps1'
$powerShellPath = (Get-Process -Id $PID).Path
$temporaryRepository = Join-Path `
  ([System.IO.Path]::GetTempPath()) `
  "dota-keyboard-release-policy-$([guid]::NewGuid().ToString('N'))"
$locationPushed = $false

function Invoke-Git {
  param(
    [Parameter(Mandatory)]
    [string[]] $Arguments
  )

  $output = @(& git @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed:`n$($output -join [Environment]::NewLine)"
  }
  return $output
}

function Assert-Validation {
  param(
    [Parameter(Mandatory)]
    [string] $Name,

    [Parameter(Mandatory)]
    [string] $Tag,

    [Parameter(Mandatory)]
    [string] $Commit,

    [Parameter(Mandatory)]
    [string] $MainRef,

    [Parameter(Mandatory)]
    [bool] $ShouldSucceed,

    [Parameter(Mandatory)]
    [string] $ExpectedText
  )

  $argumentList = @(
    '-NoProfile',
    '-File', $validatorPath,
    '-Tag', $Tag,
    '-Commit', $Commit,
    '-MainRef', $MainRef
  )
  $output = @(& $powerShellPath @argumentList 2>&1)
  $exitCode = $LASTEXITCODE
  $succeeded = $exitCode -eq 0
  $renderedOutput = ($output | Out-String).Trim()

  if ($succeeded -ne $ShouldSucceed) {
    throw "$Name expected success=$ShouldSucceed, got exit code $exitCode.`n$renderedOutput"
  }
  if ($renderedOutput -notlike "*$ExpectedText*") {
    throw "$Name expected output containing '$ExpectedText'.`n$renderedOutput"
  }

  Write-Host "PASS: $Name"
}

try {
  New-Item -ItemType Directory -Path $temporaryRepository | Out-Null
  Push-Location $temporaryRepository
  $locationPushed = $true

  Invoke-Git -Arguments @('init', '-b', 'main') | Out-Null
  Invoke-Git -Arguments @('config', 'user.name', 'Release Policy Test') | Out-Null
  Invoke-Git -Arguments @('config', 'user.email', 'release-policy@example.invalid') | Out-Null

  Set-Content -LiteralPath 'CMakeLists.txt' -Encoding utf8NoBOM -Value @'
cmake_minimum_required(VERSION 3.28)
project(dota_keyboard VERSION 0.1.0 LANGUAGES CXX)
'@
  Invoke-Git -Arguments @('add', 'CMakeLists.txt') | Out-Null
  Invoke-Git -Arguments @('commit', '-m', 'main release commit') | Out-Null
  $mainCommit = (
    Invoke-Git -Arguments @('rev-parse', '--verify', 'HEAD') |
      Select-Object -Last 1
  ).Trim()
  Invoke-Git -Arguments @(
    'update-ref',
    'refs/remotes/origin/main',
    $mainCommit
  ) | Out-Null

  Invoke-Git -Arguments @('switch', '-c', 'feature/not-on-main') | Out-Null
  Set-Content -LiteralPath 'feature.txt' -Encoding utf8NoBOM -Value 'feature'
  Invoke-Git -Arguments @('add', 'feature.txt') | Out-Null
  Invoke-Git -Arguments @('commit', '-m', 'feature commit') | Out-Null
  $featureCommit = (
    Invoke-Git -Arguments @('rev-parse', '--verify', 'HEAD') |
      Select-Object -Last 1
  ).Trim()

  Assert-Validation `
    -Name 'valid tag on origin/main' `
    -Tag 'v0.1.0' `
    -Commit $mainCommit `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $true `
    -ExpectedText 'Release policy validated'

  Assert-Validation `
    -Name 'incomplete semantic version tag' `
    -Tag 'v0.1' `
    -Commit $mainCommit `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $false `
    -ExpectedText "Tag 'v0.1' must match"

  Assert-Validation `
    -Name 'tag without v prefix' `
    -Tag 'release-0.1.0' `
    -Commit $mainCommit `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $false `
    -ExpectedText "Tag 'release-0.1.0' must match"

  Assert-Validation `
    -Name 'tag version differs from CMake' `
    -Tag 'v0.1.1' `
    -Commit $mainCommit `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $false `
    -ExpectedText "Tag version '0.1.1' does not match CMake project version '0.1.0'"

  Assert-Validation `
    -Name 'commit is not reachable from origin/main' `
    -Tag 'v0.1.0' `
    -Commit $featureCommit `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $false `
    -ExpectedText 'is not an ancestor of'

  Assert-Validation `
    -Name 'commit cannot be resolved' `
    -Tag 'v0.1.0' `
    -Commit 'missing-commit' `
    -MainRef 'refs/remotes/origin/main' `
    -ShouldSucceed $false `
    -ExpectedText "Cannot resolve commit 'missing-commit'"

  Assert-Validation `
    -Name 'main ref cannot be resolved' `
    -Tag 'v0.1.0' `
    -Commit $mainCommit `
    -MainRef 'refs/remotes/origin/missing' `
    -ShouldSucceed $false `
    -ExpectedText "Cannot resolve main ref 'refs/remotes/origin/missing'"

  Write-Host 'All 7 release policy tests passed.'
} finally {
  if ($locationPushed) {
    Pop-Location
  }
  if (Test-Path -LiteralPath $temporaryRepository) {
    Remove-Item -LiteralPath $temporaryRepository -Recurse -Force
  }
}
