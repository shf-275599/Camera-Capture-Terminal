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

$makeArgs = @('-C', (Join-Path $PSScriptRoot 'Makefile\CM4'))
if ($Jobs -gt 0) {
    $makeArgs += "-j$Jobs"
}

& $make.Source @makeArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$firmware = Join-Path $PSScriptRoot 'Makefile\CM4\build\BaseProject_CM4.elf'
$cliCandidates = @(
    $env:STM32_PROGRAMMER_CLI,
    'D:\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe',
    'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe',
    'C:\Program Files\STMicroelectronics\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
)

$cli = $cliCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $cli) {
    throw 'STM32_Programmer_CLI.exe was not found. Set STM32_PROGRAMMER_CLI or install STM32CubeProgrammer.'
}
if (-not (Test-Path $firmware)) {
    throw "CM4 firmware was not found: $firmware`nBuild it first with make -C Makefile/CM4."
}

& $cli -c port=SWD mode=UR -w $firmware -v -rst
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
