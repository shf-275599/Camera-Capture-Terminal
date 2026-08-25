[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$firmware = Join-Path $PSScriptRoot 'Makefile\CM7\build\BaseProject_CM7.elf'
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
    throw "CM7 firmware was not found: $firmware`nBuild it first with make -C Makefile/CM7."
}

& $cli -c port=SWD mode=UR -w $firmware -v -rst
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
