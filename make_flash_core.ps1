[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('CM7', 'CM4')]
    [string]$Core,
    [Parameter(Mandatory = $true)]
    [ValidateSet('A', 'B')]
    [string]$Slot,
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

$makeDirectory = Join-Path $PSScriptRoot "Makefile\$Core"
$makefile = Join-Path $makeDirectory 'Makefile'
if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "$Core Makefile was not found: $makefile" }
$targetBase = Get-MakefileVariable -Path $makefile -Name 'TARGET_BASE'

$makeArgs = @('-C', $makeDirectory, "APP_SLOT=$Slot")
if ($Jobs -gt 0) {
    $makeArgs += "-j$Jobs"
}
& $make.Source @makeArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed: $Core slot $Slot (exit $LASTEXITCODE)"
}

$buildDirectory = if ($Slot -eq 'A') { 'build' } else { 'build_B' }
$target = if ($Slot -eq 'A') { $targetBase } else { "${targetBase}_B" }
$firmware = Join-Path (Join-Path $makeDirectory $buildDirectory) "$target.elf"

$cli = Resolve-ProgrammerCli
. (Join-Path $PSScriptRoot 'local_flash_update.ps1')
[uint32]$slotIndex = if ($Slot -eq 'A') { 0 } else { 1 }
Invoke-LocalFlashUpdate -Cli $cli -Core $Core -Firmware $firmware -Slot $slotIndex -Version $Version
