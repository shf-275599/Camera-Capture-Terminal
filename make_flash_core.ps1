[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('CM7', 'CM4')]
    [string]$Core,
    [ValidateRange(0, 256)]
    [int]$Jobs = 0
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

$makeDirectory = Join-Path $PSScriptRoot "Makefile\$Core"
$makeArgs = @('-C', $makeDirectory)
if ($Jobs -gt 0) {
    $makeArgs += "-j$Jobs"
}

& $make.Source @makeArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed: $Core (exit $LASTEXITCODE)"
}

$makefile = Join-Path $makeDirectory 'Makefile'
if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "$Core Makefile was not found: $makefile" }
$target = Get-MakefileVariable -Path $makefile -Name 'TARGET'
$buildDirectory = Get-MakefileVariable -Path $makefile -Name 'BUILD_DIR'
$firmware = Join-Path (Join-Path $makeDirectory $buildDirectory) "$target.elf"
if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) {
    throw "Firmware was not found: $firmware"
}

$cli = Resolve-ProgrammerCli
& $cli -c port=SWD mode=UR -w $firmware -v -rst
if ($LASTEXITCODE -ne 0) {
    throw "Flash failed: $Core (exit $LASTEXITCODE)"
}
