[CmdletBinding()]
param(
    [ValidateRange(0, 256)]
    [int]$Jobs = 0,
    [uint32]$Version = 0
)

$ErrorActionPreference = 'Stop'

function Get-MakefileVariable {
    param([string]$Path, [string]$Name)
    $matches = @(Select-String -Path $Path -Pattern "^\s*$Name\s*=\s*(\S+)\s*$" | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($matches.Count -ne 1) { throw "Makefile must contain exactly one $Name assignment: $Path" }
    return $matches[0]
}

function Resolve-ProgrammerCli {
    $command = Get-Command STM32_Programmer_CLI.exe -ErrorAction SilentlyContinue
    $candidates = @($env:STM32_PROGRAMMER_CLI)
    if ($command) { $candidates += $command.Source }
    $candidates += @('D:\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe', 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe', 'C:\Program Files\STMicroelectronics\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')
    $cli = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $cli) { throw 'STM32_Programmer_CLI.exe was not found. Set STM32_PROGRAMMER_CLI, add it to PATH, or install STM32CubeProgrammer.' }
    return $cli
}

$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) {
    throw 'make was not found. Install GNU Make or add it to PATH.'
}

$firmwares = @{}
foreach ($core in @('CM4', 'CM7')) {
    $makeDirectory = Join-Path $PSScriptRoot "Makefile\$core"
    $makefile = Join-Path $makeDirectory 'Makefile'
    if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "$core Makefile was not found: $makefile" }
    $targetBase = Get-MakefileVariable -Path $makefile -Name 'TARGET_BASE'

    $makeArgs = @('-C', $makeDirectory, 'slot-b')
    if ($Jobs -gt 0) {
        $makeArgs += "-j$Jobs"
    }
    & $make.Source @makeArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $firmwares[$core] = Join-Path (Join-Path $makeDirectory 'build_B') "${targetBase}_B.elf"
}

$cli = Resolve-ProgrammerCli
. (Join-Path $PSScriptRoot 'local_flash_update.ps1')
Invoke-LocalFlashUpdate -Cli $cli -Firmwares $firmwares -Slot 1 -Version $Version
