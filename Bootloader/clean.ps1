[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-MakefileVariable {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [string]$Name
    )

    $matches = @(Select-String -LiteralPath $Path -Pattern "^\s*$Name\s*=\s*(\S+)\s*$" | ForEach-Object {
        $_.Matches[0].Groups[1].Value
    })

    if ($matches.Count -ne 1) {
        throw "Makefile must contain exactly one $Name assignment: $Path"
    }

    return $matches[0]
}

function Get-SafeBuildDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$CoreDirectory,

        [Parameter(Mandatory)]
        [string]$BuildDirectory
    )

    if ([IO.Path]::IsPathRooted($BuildDirectory)) {
        throw "BUILD_DIR must be relative to the core directory: $BuildDirectory"
    }

    $corePath = [IO.Path]::GetFullPath($CoreDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $buildPath = [IO.Path]::GetFullPath((Join-Path $corePath $BuildDirectory)).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)

    if (-not $buildPath.StartsWith("$corePath$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)) {
        throw "BUILD_DIR must be inside the core directory: $BuildDirectory"
    }

    return $buildPath
}

$cores = @('CM4', 'CM7')
$make = Get-Command make, make.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$runningOnWindows = $env:OS -eq 'Windows_NT'

foreach ($core in $cores) {
    $coreDirectory = Join-Path $PSScriptRoot "Makefile\$core"
    $makefile = Join-Path $coreDirectory 'Makefile'

    if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) {
        throw "Makefile was not found: $makefile"
    }

    $buildDirectory = Get-SafeBuildDirectory -CoreDirectory $coreDirectory -BuildDirectory (Get-MakefileVariable -Path $makefile -Name 'BUILD_DIR')

    if ($runningOnWindows) {
        if (Test-Path -LiteralPath $buildDirectory) {
            Write-Host "Cleaning $core build directory..."
            Remove-Item -LiteralPath $buildDirectory -Recurse -Force
        }
        else {
            Write-Host "Build directory does not exist: $buildDirectory"
        }
    }
    elseif ($make) {
        Write-Host "Cleaning $core build directory with make..."
        & $make.Source -C $coreDirectory clean
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    elseif (Test-Path -LiteralPath $buildDirectory) {
        Write-Host "GNU Make was not found; removing $buildDirectory..."
        Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }
    else {
        Write-Host "GNU Make was not found and build directory does not exist: $buildDirectory"
    }
}

Write-Host 'Build cleanup completed.'
