[CmdletBinding()]
param(
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

$firmwares = @{}
foreach ($core in @('CM4', 'CM7')) {
    $makeDirectory = Join-Path $PSScriptRoot "Makefile\$core"
    $makefile = Join-Path $makeDirectory 'Makefile'
    if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "$core Makefile was not found: $makefile" }
    $targetBase = Get-MakefileVariable -Path $makefile -Name 'TARGET_BASE'

    $makeArgs = @('-C', $makeDirectory, 'all-slots')
    if ($Jobs -gt 0) {
        $makeArgs += "-j$Jobs"
    }
    & $make.Source @makeArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    foreach ($slot in @('A', 'B')) {
        $buildDirectory = if ($slot -eq 'A') { 'build' } else { 'build_B' }
        $targetName = if ($slot -eq 'A') { $targetBase } else { "${targetBase}_$slot" }
        $firmwares["$core-$slot"] = Join-Path (Join-Path $makeDirectory $buildDirectory) "$targetName.elf"
    }
}

$flashOrder = @('CM7-A', 'CM4-A', 'CM7-B', 'CM4-B')
foreach ($name in $flashOrder) {
    $firmware = $firmwares[$name]
    if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) {
        throw "Firmware was not found: $firmware"
    }
}

$cli = Resolve-ProgrammerCli
foreach ($name in $flashOrder) {
    $programmerArgs = @('-c', 'port=SWD', 'mode=UR', '-w', $firmwares[$name], '-v')
    if ($name -eq 'CM4-B') {
        $programmerArgs += '-rst'
    }
    & $cli @programmerArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
