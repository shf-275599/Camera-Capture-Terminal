$ErrorActionPreference = 'Stop'
$entries = @('make_flash_cm7_slotA.ps1', 'make_flash_cm7_slotB.ps1', 'make_flash_cm4_slotA.ps1', 'make_flash_cm4_slotB.ps1')

function Assert-UpdateTest {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

# 模拟设备立即返回结果，无需等待真实硬件的 CRC / EEPROM 时序。
function Start-Sleep {
    param([int]$Seconds, [int]$Milliseconds)
}

# 模拟全部编程器操作，不连接设备。
function Invoke-UpdateProgrammer {
    param([string]$Cli, [string[]]$Arguments)
    [void]$script:calls.Add($Arguments)
    if ($Arguments -contains '0x4C555031') { $script:phase = 'check' }
    if ($Arguments -contains '0x4C555032') { $script:phase = 'commit' }
    if ($script:scenario -eq 'flash-failed' -and $Arguments -contains '-w') { throw 'Simulated Flash failure' }
    $upload = [Array]::IndexOf($Arguments, '-u')
    if ($upload -lt 0) { return }
    if ($Arguments[$upload + 1] -eq '0x0801FFF0') {
        $magic = if ($script:scenario -eq 'old-bootloader') { 0 } else { 0x4C555044 }
        $protocol = if ($script:scenario -eq 'v1-bootloader') { 1 } else { 2 }
        $words = @($magic, $protocol, 0x3800F100, 32)
    } else {
        $failed = ($script:phase -eq 'check' -and $script:scenario -eq 'check-failed') -or
                  ($script:phase -eq 'commit' -and $script:scenario -eq 'commit-failed')
        $words = if ($failed) { @(3) } else { @(2) }
    }
    $bytes = New-Object 'System.Collections.Generic.List[byte]'
    foreach ($word in $words) { $bytes.AddRange([BitConverter]::GetBytes([uint32]$word)) }
    [IO.File]::WriteAllBytes($Arguments[$upload + 3], $bytes.ToArray())
}

$directory = Join-Path $HOME ('temp\local-update-test-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($directory)
try {
    foreach ($entry in $entries) {
        $tokens = $null; $parseErrors = $null
        $entryAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "..\$entry"), [ref]$tokens, [ref]$parseErrors)
        Assert-UpdateTest ($parseErrors.Count -eq 0) "Syntax error: $entry"
        # 仅加载函数，不执行入口中的真实构建、烧录命令；保留编程器模拟函数。
        foreach ($function in $entryAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
            if ($function.Name -ne 'Invoke-UpdateProgrammer') {
                . ([scriptblock]::Create($function.Extent.Text))
            }
        }
    foreach ($slot in @(0, 1)) {
        $firmwares = @{}
        foreach ($core in @('CM7', 'CM4')) {
            [uint32]$base = if ($core -eq 'CM7') { 0x08020000 } else { 0x08120000 }
            if ($slot -eq 1) { $base += 0x80000 }
            $elf = Join-Path $directory "$core.elf"
            [IO.File]::WriteAllBytes($elf, [byte[]]@(0))
            $bytes = New-Object byte[] 16
            [BitConverter]::GetBytes([uint32]0x20010000).CopyTo($bytes, 0)
            [BitConverter]::GetBytes([uint32]($base + 9)).CopyTo($bytes, 4)
            [IO.File]::WriteAllBytes([IO.Path]::ChangeExtension($elf, '.bin'), $bytes)
            $firmwares[$core] = $elf
        }
        foreach ($core in @('CM7', 'CM4')) {
        foreach ($scenario in @('success', 'old-bootloader', 'v1-bootloader', 'check-failed', 'commit-failed', 'flash-failed')) {
            $script:scenario = $scenario
            $script:phase = ''
            $script:calls = New-Object 'System.Collections.Generic.List[object]'
            $failed = $false
            try { Invoke-LocalFlashUpdate -Cli 'mock' -Core $core -Firmware $firmwares[$core] -Slot $slot -Version 42 }
            catch { $failed = $true }
            Assert-UpdateTest ($failed -eq ($scenario -ne 'success')) "Unexpected result: $scenario"
            $writes = @($script:calls | Where-Object { $_ -contains '-w' })
            $requests = @($script:calls | Where-Object { $_ -contains '0x3800F104' })
            if ($scenario -in @('old-bootloader', 'v1-bootloader', 'check-failed')) {
                Assert-UpdateTest ($writes.Count -eq 0) 'Must reject before Flash erase'
            }
            if ($scenario -eq 'success') {
                Assert-UpdateTest ($writes.Count -eq 1 -and $requests.Count -eq 2) 'Missing single-core check/write/commit stages'
                Assert-UpdateTest ($writes[0][4] -eq $firmwares[$core]) 'Wrong core image written'
                $coreId = if ($core -eq 'CM7') { 7 } else { 4 }
                foreach ($request in $requests) {
                    Assert-UpdateTest ($request[5] -eq ('0x{0:X8}' -f $coreId)) 'Wrong target core'
                    Assert-UpdateTest ($request[6] -eq ('0x{0:X8}' -f $slot)) 'Wrong target slot'
                    Assert-UpdateTest ($request[9] -eq '0x0000002A' -and $request[10] -eq '0x00000000') 'Wrong version/reserved word'
                }
                Assert-UpdateTest ($script:calls[$script:calls.Count - 1] -contains '-rst') 'Missing trial boot reset'
                $checkIndex = -1; $writeIndex = -1; $commitIndex = -1
                for ($i = 0; $i -lt $script:calls.Count; $i++) {
                    if ($script:calls[$i] -contains '0x4C555031') { $checkIndex = $i }
                    if ($writeIndex -lt 0 -and $script:calls[$i] -contains '-w') { $writeIndex = $i }
                    if ($script:calls[$i] -contains '0x4C555032') { $commitIndex = $i }
                }
                Assert-UpdateTest ($checkIndex -lt $writeIndex -and $writeIndex -lt $commitIndex) 'Unsafe update ordering'
            } else {
                Assert-UpdateTest (-not ($script:calls[$script:calls.Count - 1] -contains '-rst')) 'Failure must not perform final reset'
            }
        }
        }
    }
    }
    foreach ($file in $entries) {
        $tokens = $null; $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "..\$file"), [ref]$tokens, [ref]$parseErrors)
        Assert-UpdateTest ($parseErrors.Count -eq 0) "Syntax error: $file"
        $parameter = @($ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Version' })
        Assert-UpdateTest ($parameter.Count -eq 1 -and $parameter[0].DefaultValue.Value -eq 0) 'Version must default to zero'
        if ($file -match '^make_flash_(cm[47])_slot([AB])\.ps1$') {
            $expectedCore = $Matches[1].ToUpperInvariant()
            $slotLetter = $Matches[2]
            $expectedSlot = if ($Matches[2] -eq 'A') { 0 } else { 1 }
            $commands = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true) |
                Where-Object { $_.Extent.Text -match '-Core\s+CM[47]' })
            Assert-UpdateTest ($commands.Count -eq 1) "Missing core dispatch: $file"
            $expectedDispatch = '-Core {0} -Firmware \$firmware -Slot {1} -Version \$Version' -f $expectedCore, $expectedSlot
            Assert-UpdateTest ($commands[0].Extent.Text -match $expectedDispatch) "Wrong dispatch: $file"
            $source = $ast.Extent.Text
            Assert-UpdateTest ($source.Contains("Makefile\$expectedCore")) "Wrong build core: $file"
            Assert-UpdateTest ($source.Contains("APP_SLOT=$slotLetter")) "Wrong build slot: $file"
            $expectedFirmware = if ($slotLetter -eq 'A') { '"$targetBase.elf"' } else { '"${targetBase}_B.elf"' }
            $expectedDirectory = if ($slotLetter -eq 'A') { "'build'" } else { "'build_B'" }
            Assert-UpdateTest ($source.Contains($expectedFirmware) -and $source.Contains($expectedDirectory)) "Wrong firmware path: $file"
        }
    }
    Write-Host 'PASS: CM7/CM4 A/B single-image writes, request core/slot/version, protocol v1 rejection, update ordering and failure paths'
}
finally {
    Remove-Item -LiteralPath $directory -Recurse -Force
}
