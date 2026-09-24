[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

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
$buildDirectories = @('build', 'build_A', 'build_B')

foreach ($core in $cores) {
    $coreDirectory = Join-Path $PSScriptRoot "Makefile\$core"
    $makefile = Join-Path $coreDirectory 'Makefile'

    if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) {
        throw "Makefile was not found: $makefile"
    }

    foreach ($name in $buildDirectories) {
        $buildDirectory = Get-SafeBuildDirectory -CoreDirectory $coreDirectory -BuildDirectory $name
        if (Test-Path -LiteralPath $buildDirectory) {
            Write-Host "Cleaning $core $name directory..."
            Remove-Item -LiteralPath $buildDirectory -Recurse -Force
        }
        else {
            Write-Host "Build directory does not exist: $buildDirectory"
        }
    }
}

Write-Host 'Build cleanup completed.'
