[CmdletBinding()]
param(
    [ValidateRange(0, 256)]
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) {
    throw 'make was not found. Install GNU Make or add it to PATH.'
}

foreach ($core in @('CM4', 'CM7')) {
    $makeDirectory = Join-Path $PSScriptRoot "Makefile\$core"
    $makefile = Join-Path $makeDirectory 'Makefile'
    if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "$core Makefile was not found: $makefile" }

    $makeArgs = @('-C', $makeDirectory, 'all-slots')
    if ($Jobs -gt 0) {
        $makeArgs += "-j$Jobs"
    }
    & $make.Source @makeArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
