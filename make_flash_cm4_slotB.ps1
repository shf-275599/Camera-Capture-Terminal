[CmdletBinding()]
param(
    [ValidateRange(0, 256)]
    [int]$Jobs = 0,
    [uint32]$Version = 0
)
$ErrorActionPreference = 'Stop'

# 独立入口：构建 CM4 B 槽，再执行本地协议 v2 升级。
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

function Invoke-UpdateProgrammer {
    param([string]$Cli, [string[]]$Arguments)
    & $Cli @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "STM32CubeProgrammer failed: $LASTEXITCODE" }
}

function Read-UpdateWords {
    param([string]$Cli, [string]$Address, [int]$Size, [string]$Path, [switch]$Resume)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path }
    $arguments = @('-c', 'port=SWD', 'mode=HOTPLUG', '-u', $Address, "$Size", $Path)
    if ($Resume) { $arguments += '-run' }
    Invoke-UpdateProgrammer $Cli $arguments
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ne $Size) { throw 'Device read size mismatch' }
    for ($i = 0; $i -lt $Size; $i += 4) { [BitConverter]::ToUInt32($bytes, $i) }
}

function Get-UpdateImageInfo {
    param([string]$Elf, [uint32]$Base)
    if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) { throw "Missing ELF: $Elf" }
    $bin = [IO.Path]::ChangeExtension($Elf, '.bin')
    $bytes = [IO.File]::ReadAllBytes($bin)
    if ($bytes.Length -lt 8 -or $bytes.Length -gt 393216) { throw "Invalid image size: $bin" }
    $entry = [BitConverter]::ToUInt32($bytes, 4)
    if (($entry -band 1) -eq 0 -or $entry -lt $Base -or $entry -ge ([uint64]$Base + $bytes.Length)) {
        throw "Image reset vector does not match target slot: $bin"
    }
    $table = New-Object 'uint32[]' 256
    for ($i = 0; $i -lt 256; $i++) {
        [uint32]$value = $i
        for ($bit = 0; $bit -lt 8; $bit++) {
            if ($value -band 1) { $value = ($value -shr 1) -bxor [uint32]3988292384 }
            else { $value = $value -shr 1 }
        }
        $table[$i] = $value
    }
    [uint32]$crc = [uint32]::MaxValue
    foreach ($value in $bytes) { $crc = ($crc -shr 8) -bxor $table[($crc -bxor $value) -band 255] }
    return @([uint32]$bytes.Length, [uint32]($crc -bxor [uint32]::MaxValue))
}

function Invoke-UpdateRequest {
    param([string]$Cli, [uint32]$Command, [uint32[]]$Words, [string]$ReadPath)
    $arguments = @('-c', 'port=SWD', 'mode=UR', '-w32', '0x3800F104')
    foreach ($word in $Words) { $arguments += ('0x{0:X8}' -f $word) }
    $arguments += @('0x00000000', '-w32', '0x3800F100', ('0x{0:X8}' -f $Command), '-rst')
    Invoke-UpdateProgrammer $Cli $arguments
    # 先让 CRC 校验和 I2C 页写结束，避免 SWD 暂停内核打断总线时序。
    Start-Sleep -Seconds 2
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 250
        $status = @(Read-UpdateWords $Cli '0x3800F11C' 4 $ReadPath -Resume)[0]
        if ($status -eq 3) { throw 'Bootloader rejected update: EEPROM unavailable/invalid, pending exists, target is confirmed, or image verification failed. No final reset performed.' }
        if ($status -eq 2) { return }
    } while ($timer.Elapsed.TotalSeconds -lt 30)
    throw 'Bootloader update request timed out; result is unknown. No final reset performed.'
}

function Invoke-LocalFlashUpdate {
    param([string]$Cli, [ValidateSet('CM7', 'CM4')][string]$Core, [string]$Firmware, [ValidateRange(0, 1)][uint32]$Slot, [uint32]$Version = 0)
    [uint32]$base = if ($Core -eq 'CM7') { 0x08020000 } else { 0x08120000 }
    if ($Slot -eq 1) { $base += 0x80000 }
    $image = Get-UpdateImageInfo $Firmware $base
    $directory = Join-Path $HOME ('temp\local-update-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    try {
        $readPath = Join-Path $directory 'read.bin'
        Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=UR', '-halt')
        $descriptor = @(Read-UpdateWords $Cli '0x0801FFF0' 16 $readPath)
        if ($descriptor[0] -ne 0x4C555044 -or $descriptor[1] -ne 2 -or
            $descriptor[2] -ne 0x3800F100 -or $descriptor[3] -ne 32) {
            throw 'Flash the updated Bootloader first. Application Flash was not written.'
        }
        [uint32]$coreId = if ($Core -eq 'CM7') { 7 } else { 4 }
        [uint32[]]$words = @($coreId, $Slot, $image[0], $image[1], $Version, 0)
        # 应用擦写前先保护双核 confirmed 镜像，检查阶段不写 EEPROM。
        Invoke-UpdateRequest $Cli 0x4C555031 $words $readPath
        Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=UR', '-w', $Firmware, '-v')
        Invoke-UpdateRequest $Cli 0x4C555032 $words $readPath
        Write-Host "PENDING: core=$Core slot=$Slot version=$Version; target core has 3 trials. Runtime confirmation is still required."
        Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=HOTPLUG', '-rst')
    }
    finally {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
}

$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) { throw 'make was not found. Install GNU Make or add it to PATH.' }
$makeDirectory = Join-Path $PSScriptRoot 'Makefile\CM4'
$makefile = Join-Path $makeDirectory 'GNUmakefile'
if (-not (Test-Path -LiteralPath $makefile -PathType Leaf)) { throw "CM4 Makefile was not found: $makefile" }
$targetBase = Get-MakefileVariable -Path $makefile -Name 'TARGET_BASE'
$makeArgs = @('-C', $makeDirectory, 'APP_SLOT=B')
if ($Jobs -gt 0) { $makeArgs += "-j$Jobs" }
& $make.Source @makeArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed: CM4 slot B (exit $LASTEXITCODE)" }
$firmware = Join-Path (Join-Path $makeDirectory 'build_B') "${targetBase}_B.elf"
$cli = Resolve-ProgrammerCli
Invoke-LocalFlashUpdate -Cli $cli -Core CM4 -Firmware $firmware -Slot 1 -Version $Version
