param([string[]]$Scenarios = @('begin', 'chunk', 'network', 'partial', 'restart', 'changed', 'expired', 'end'))
$ErrorActionPreference = 'Stop'
$firmware = (Resolve-Path (Join-Path $PSScriptRoot '../BaseProject/Makefile/CM7/build_B/BaseProject_CM7_B.bin')).Path
$exe = Join-Path $PSScriptRoot 'qt-ota.exe'
$previousTask = $env:QT_OTA_TASK_FILE
$testDir = Join-Path $HOME ('temp/qt-ota-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
try {
    foreach ($scenario in $Scenarios) {
        $env:QT_OTA_TASK_FILE = Join-Path $testDir "$scenario.json"
        $job = Start-Job -ArgumentList $firmware,$scenario -ScriptBlock {
            param($firmwarePath,$mode)
            $image = [IO.File]::ReadAllBytes($firmwarePath)
            $offset = 0; $begun = $false; $injected = $false; $ended = $false
            $listener = [Net.HttpListener]::new()
            $listener.Prefixes.Add('http://127.0.0.1:18759/')
            $listener.Start()
            try {
                'READY'
                while ($true) {
                    $pending = $listener.GetContextAsync()
                    if (-not $pending.Wait(20000)) { throw '等待客户端超时' }
                    $context = $pending.Result
                    $request = $context.Request
                    $text = ''; $code = 200; $done = $false; $disconnect = $false; $skipResponse = $false
                    switch ($request.Url.AbsolutePath) {
                        '/ota/status' {
                            if ($ended) {
                                $text = "busy=0`nother_core_busy=0`nflags=0x00`nrunning=1`nversion_b=301`nsize_b=$($image.Length)`n"
                                $done = $true
                            } else { $text = "busy=$([int]$begun)`nother_core_busy=0`nflags=0x00`nrunning=0`n" }
                        }
                        '/ota/begin' {
                            if ([int]$request.Headers['X-Fw-Size'] -ne $image.Length -or
                                $request.Headers['X-Fw-Base'] -ne '0x80a0000' -or
                                $request.Headers['X-Fw-Version'] -ne '301' -or
                                $request.Headers['X-Ota-Core'] -ne 'cm7') { throw '恢复元数据错误' }
                            if ($begun) { $text = "OTA resume written=$offset" }
                            else { $begun = $true; $text = 'OTA ready written=0' }
                            if ($mode -eq 'begin' -and -not $injected) { $injected=$true; $disconnect=$true }
                        }
                        '/ota/chunk' {
                            if ([int]$request.Headers['X-Offset'] -ne $offset) { throw "恢复偏移错误，expect=$offset" }
                            $memory = [IO.MemoryStream]::new()
                            try {
                                $request.InputStream.CopyTo($memory)
                                $bytes = $memory.ToArray()
                                if ($bytes.Length -lt 1 -or $bytes.Length -gt 8192) { throw '块长度错误' }
                                for ($i=0; $i -lt $bytes.Length; $i++) {
                                    if ($bytes[$i] -ne $image[$offset+$i]) { throw '恢复数据错误' }
                                }
                                if ($mode -ne 'partial' -or $injected) { $offset += $bytes.Length }
                            } finally { $memory.Dispose() }
                            $text = "written=$offset"
                            if (-not $injected) {
                                $injected = $true
                                if ($mode -eq 'chunk') { $disconnect=$true }
                                if ($mode -in @('network','partial')) {
                                    # 故意发送不完整回包；partial 模式模拟设备未提交该块。
                                    $context.Response.ContentLength64=100
                                    $context.Response.OutputStream.WriteByte(65)
                                    $context.Response.Abort()
                                    $skipResponse = $true
                                    continue
                                }
                                if ($mode -in @('restart','changed','expired')) {
                                    'CHECKPOINT'
                                    Start-Sleep -Seconds 2
                                    $disconnect=$true
                                    if ($mode -eq 'expired') { $begun=$false; $offset=0 }
                                }
                            }
                        }
                        '/ota/end' {
                            if ($offset -ne $image.Length) { throw '结束长度错误' }
                            $ended=$true; $text='OTA OK, rebooting'
                            if ($mode -eq 'end') { $disconnect=$true } else { $done=$true }
                        }
                        '/ota/abort' { throw '故障恢复不应发送 abort' }
                        default { throw '意外请求' }
                    }
                    if ($skipResponse) { continue }
                    if ($disconnect) {
                        # 在实际处理后返回不确定性失败，验证以设备偏移为准而非重发原块。
                        $code=503; $text='injected response failure'
                    }
                    try {
                        $body=[Text.Encoding]::UTF8.GetBytes($text)
                        $context.Response.StatusCode=$code
                        $context.Response.ContentLength64=$body.Length
                        $context.Response.OutputStream.Write($body,0,$body.Length)
                        $context.Response.Close()
                    } catch { if ($mode -notin @('restart','changed','expired')) { throw } }
                    if ($done) { "RECOVERY PASS $mode bytes=$offset"; break }
                }
            } finally { $listener.Stop(); $listener.Close() }
        }
        $process = $null
        try {
            $ready=$false
            for ($i=0; $i -lt 100; $i++) {
                if (@(Receive-Job $job -Keep) -contains 'READY') { $ready=$true; break }
                Start-Sleep -Milliseconds 100
            }
            if (-not $ready) { throw '模拟设备未就绪' }
            $process=Start-Process $exe -ArgumentList ('--test-upload http://127.0.0.1:18759 "' + $firmware + '"') -PassThru
            if ($scenario -in @('restart','changed','expired')) {
                $checkpoint=$false
                for ($i=0; $i -lt 100; $i++) {
                    if (@(Receive-Job $job -Keep) -contains 'CHECKPOINT') { $checkpoint=$true; break }
                    Start-Sleep -Milliseconds 100
                }
                if (-not $checkpoint) { throw '进程中断检查点未到达' }
                $process.Kill(); $process.WaitForExit()
                if (-not (Test-Path $env:QT_OTA_TASK_FILE)) { throw '未保存任务' }
                if ($scenario -eq 'changed') {
                    $task=Get-Content $env:QT_OTA_TASK_FILE -Raw | ConvertFrom-Json
                    $task.sha256='00'
                    $task | ConvertTo-Json | Set-Content $env:QT_OTA_TASK_FILE -Encoding UTF8
                }
                $process=Start-Process $exe -ArgumentList '--test-resume' -PassThru
            }
            if (-not $process.WaitForExit(30000)) { throw '恢复测试超时' }
            if ($scenario -in @('changed','expired')) {
                if ($process.ExitCode -eq 0 -or -not (Test-Path $env:QT_OTA_TASK_FILE)) { throw '应拒绝恢复并保留任务' }
                "RECOVERY PASS $scenario rejected safely"
            } else {
                if ($process.ExitCode -ne 0) { throw "Qt 恢复失败 $scenario" }
                $null=Wait-Job $job -Timeout 5
                $result=@(Receive-Job $job)
                if (-not ($result -match 'RECOVERY PASS')) { throw "服务端验证失败 $scenario" }
                if (Test-Path $env:QT_OTA_TASK_FILE) { throw '成功后任务未清理' }
                $result | Where-Object { $_ -match 'RECOVERY PASS' }
            }
        } finally {
            if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            Stop-Job $job; Remove-Job $job
        }
    }
} finally {
    $env:QT_OTA_TASK_FILE=$previousTask
    Remove-Item $testDir -Recurse -Force
}
