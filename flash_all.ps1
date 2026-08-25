[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$cm7Firmware = Join-Path $PSScriptRoot 'Makefile\CM7\build\BaseProject_CM7.elf'
$cm4Firmware = Join-Path $PSScriptRoot 'Makefile\CM4\build\BaseProject_CM4.elf'
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
foreach ($firmware in @($cm7Firmware, $cm4Firmware)) {
    if (-not (Test-Path $firmware)) {
        throw "Firmware was not found: $firmware`nBuild both cores first with make -C Makefile/CM7 and make -C Makefile/CM4."
    }
}

& $cli -c port=SWD mode=UR -w $cm7Firmware -v
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cli -c port=SWD mode=UR -w $cm4Firmware -v -rst
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
