$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\local_flash_update.ps1')

function Assert-UpdateTest {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
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
        $words = @($magic, 1, 0x3800F100, 32)
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
        foreach ($scenario in @('success', 'old-bootloader', 'check-failed', 'commit-failed', 'flash-failed')) {
            $script:scenario = $scenario
            $script:phase = ''
            $script:calls = New-Object 'System.Collections.Generic.List[object]'
            $failed = $false
            try { Invoke-LocalFlashUpdate -Cli 'mock' -Firmwares $firmwares -Slot $slot -Version 42 }
            catch { $failed = $true }
            Assert-UpdateTest ($failed -eq ($scenario -ne 'success')) "Unexpected result: $scenario"
            $writes = @($script:calls | Where-Object { $_ -contains '-w' })
            $requests = @($script:calls | Where-Object { $_ -contains '0x3800F104' })
            if ($scenario -eq 'old-bootloader' -or $scenario -eq 'check-failed') {
                Assert-UpdateTest ($writes.Count -eq 0) 'Must reject before Flash erase'
            }
            if ($scenario -eq 'success') {
                Assert-UpdateTest ($writes.Count -eq 2 -and $requests.Count -eq 2) 'Missing check/write/commit stages'
                Assert-UpdateTest ($requests[0][5] -eq ('0x{0:X8}' -f $slot)) 'Wrong target slot'
                Assert-UpdateTest ($requests[1][10] -eq '0x0000002A') 'Wrong version'
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
    foreach ($file in @('make_flash_slotA.ps1', 'make_flash_slotB.ps1', 'local_flash_update.ps1')) {
        $tokens = $null; $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "..\$file"), [ref]$tokens, [ref]$parseErrors)
        Assert-UpdateTest ($parseErrors.Count -eq 0) "Syntax error: $file"
        if ($file -ne 'local_flash_update.ps1') {
            $parameter = @($ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Version' })
            Assert-UpdateTest ($parameter.Count -eq 1 -and $parameter[0].DefaultValue.Value -eq 0) 'Version must default to zero'
        }
    }
    Write-Host 'PASS: A/B update ordering, version transfer/default, preflight rejection, Flash failure, pending failure'
}
finally {
    Remove-Item -LiteralPath $directory -Recurse -Force
}
