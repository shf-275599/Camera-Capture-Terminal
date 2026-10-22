$ErrorActionPreference = 'Stop'
$firmware = (Resolve-Path (Join-Path $PSScriptRoot '../BaseProject/Makefile/CM7/build_B/BaseProject_CM7_B.bin')).Path
$job = Start-Job -ArgumentList $firmware -ScriptBlock {
    param($path)
    $image = [IO.File]::ReadAllBytes($path)
    $offset = 0
    $listener = [Net.HttpListener]::new()
    $listener.Prefixes.Add('http://127.0.0.1:18758/')
    $listener.Start()
    try {
        'READY'
        while ($true) {
            $task = $listener.GetContextAsync()
            if (-not $task.Wait(15000)) { throw '请求超时。' }
            $context = $task.Result
            $request = $context.Request
            $text = 'OK'
            $done = $false
            switch ($request.Url.AbsolutePath) {
                '/ota/status' { $text = "busy=0`nother_core_busy=0`nflags=0x00`nrunning=0`n" }
                '/ota/begin' {
                    if ($request.Headers['X-Fw-Base'] -ne '0x80a0000' -or
                        [int]$request.Headers['X-Fw-Size'] -ne $image.Length -or
                        $request.Headers['X-Ota-Core'] -ne 'cm7') { throw '元数据错误。' }
                }
                '/ota/chunk' {
                    if ([int]$request.Headers['X-Offset'] -ne $offset) { throw 'offset 错误。' }
                    $memory = [IO.MemoryStream]::new()
                    try {
                        $request.InputStream.CopyTo($memory)
                        $bytes = $memory.ToArray()
                        if ($bytes.Length -lt 1 -or $bytes.Length -gt 8192) { throw '块大小错误。' }
                        for ($i=0; $i -lt $bytes.Length; $i++) {
                            if ($bytes[$i] -ne $image[$offset+$i]) { throw '固件数据不一致。' }
                        }
                        $offset += $bytes.Length
                    } finally { $memory.Dispose() }
                }
                '/ota/end' {
                    if ($offset -ne $image.Length) { throw '镜像长度不一致。' }
                    $done = $true
                    $text = 'OTA OK, rebooting'
                }
                default { throw '意外请求。' }
            }
            $body = [Text.Encoding]::UTF8.GetBytes($text)
            $context.Response.ContentLength64 = $body.Length
            $context.Response.OutputStream.Write($body,0,$body.Length)
            $context.Response.Close()
            if ($done) { "QT UPLOAD PASS bytes=$offset"; break }
        }
    } finally { $listener.Stop(); $listener.Close() }
}
try {
    $ready = $false
    for ($i=0; $i -lt 50; $i++) {
        if (@(Receive-Job $job -Keep) -contains 'READY') { $ready=$true; break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw '模拟设备启动失败。' }
    $arguments = '--test-upload http://127.0.0.1:18758 "' + $firmware + '"'
    $process = Start-Process (Join-Path $PSScriptRoot 'qt-ota.exe') -ArgumentList $arguments -PassThru
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Qt 上传测试超时。' }
    if ($process.ExitCode) { throw "Qt 返回错误 $($process.ExitCode)" }
    $null = Wait-Job $job -Timeout 5
    $result = @(Receive-Job $job)
    $result
    if (-not ($result -match 'QT UPLOAD PASS')) { throw 'Qt 上传验证未通过。' }
} finally { Stop-Job $job; Remove-Job $job }
