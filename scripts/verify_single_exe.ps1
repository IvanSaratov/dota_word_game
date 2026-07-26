[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidateNotNullOrEmpty()]
  [string]$Executable
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
  throw "Executable was not found: $Executable"
}
if (-not $env:WINDIR) {
  throw 'WINDIR is not set.'
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($dumpbin) {
  $dumpbinPath = $dumpbin.Path
} else {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'dumpbin.exe was not found and vswhere.exe is unavailable.'
  }
  $visualStudioPath = @(
    & $vswhere -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
  )[0]
  if (-not $visualStudioPath) {
    throw 'No Visual Studio installation with the x64 C++ tools was found.'
  }
  $msvcRoot = Join-Path $visualStudioPath 'VC\Tools\MSVC'
  $dumpbinCandidates = @(
    Get-ChildItem -LiteralPath $msvcRoot -Directory |
      Sort-Object Name -Descending |
      ForEach-Object {
        $candidate = Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
          $candidate
        }
      }
  )
  if ($dumpbinCandidates.Count -eq 0) {
    throw 'Visual Studio x64 dumpbin.exe was not found.'
  }
  $dumpbinPath = $dumpbinCandidates[0]
}

$dumpbinOutput = @(& $dumpbinPath /dependents $Executable 2>&1)
$dumpbinExitCode = $LASTEXITCODE
$dumpbinOutput | ForEach-Object { Write-Host $_ }
if ($dumpbinExitCode -ne 0) {
  throw "dumpbin /dependents exited with $dumpbinExitCode."
}

$dependencyNames = @(
  $dumpbinOutput |
    ForEach-Object {
      $match = [regex]::Match(
        [string]$_,
        '^\s*([A-Za-z0-9._-]+\.dll)\s*$',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
      )
      if ($match.Success) {
        $match.Groups[1].Value
      }
    } |
    Sort-Object -Unique
)

$rejectedDependencies = @(
  $dependencyNames | Where-Object {
    $_ -match '(?i)(opencv|onnxruntime|vcruntime|msvcp|concrt|ucrtbase)'
  }
)
$system32 = Join-Path $env:WINDIR 'System32'
$unresolvedDependencies = @(
  $dependencyNames |
    Where-Object {
      $_ -notin $rejectedDependencies -and
      -not (Test-Path -LiteralPath (Join-Path $system32 $_) -PathType Leaf)
    }
)

if ($rejectedDependencies -or $unresolvedDependencies) {
  if ($rejectedDependencies) {
    Write-Host "Rejected dependencies: $($rejectedDependencies -join ', ')"
  } else {
    Write-Host 'Rejected dependencies: none'
  }
  if ($unresolvedDependencies) {
    Write-Host "Dependencies not resolved by System32: $($unresolvedDependencies -join ', ')"
  }
  exit 1
}

Write-Host "All dependencies resolve under $system32."
