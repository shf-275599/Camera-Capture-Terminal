# 本地升级采用与 OTA 相同的 pending / 试启动 / 应用确认语义。
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
    # SWD 上传会短暂暂停内核；先让 CRC 校验和 I2C 页写完整结束，避免打断总线时序。
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
    param([string]$Cli, [hashtable]$Firmwares, [ValidateRange(0, 1)][uint32]$Slot, [uint32]$Version = 0)
    [uint32]$cm7Base = if ($Slot -eq 0) { 0x08020000 } else { 0x080A0000 }
    [uint32]$cm4Base = if ($Slot -eq 0) { 0x08120000 } else { 0x081A0000 }
    $cm7 = Get-UpdateImageInfo $Firmwares['CM7'] $cm7Base
    $cm4 = Get-UpdateImageInfo $Firmwares['CM4'] $cm4Base
    $directory = Join-Path $HOME ('temp\local-update-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    try {
        $readPath = Join-Path $directory 'read.bin'
        Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=UR', '-halt')
        $descriptor = @(Read-UpdateWords $Cli '0x0801FFF0' 16 $readPath)
        if ($descriptor[0] -ne 0x4C555044 -or $descriptor[1] -ne 1 -or
            $descriptor[2] -ne 0x3800F100 -or $descriptor[3] -ne 32) {
            throw 'Flash the updated Bootloader first. Application Flash was not written.'
        }
        [uint32[]]$words = @($Slot, $cm7[0], $cm7[1], $cm4[0], $cm4[1], $Version)
        # 检查阶段不写 EEPROM，且在任何应用擦写前保护双核 confirmed 镜像。
        Invoke-UpdateRequest $Cli 0x4C555031 $words $readPath
        foreach ($core in @('CM7', 'CM4')) {
            Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=UR', '-w', $Firmwares[$core], '-v')
        }
        Invoke-UpdateRequest $Cli 0x4C555032 $words $readPath
        Write-Host "PENDING: slot=$Slot version=$Version; both cores have 3 trials. Runtime confirmation is still required."
        Invoke-UpdateProgrammer $Cli @('-c', 'port=SWD', 'mode=HOTPLUG', '-rst')
    }
    finally {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
}
