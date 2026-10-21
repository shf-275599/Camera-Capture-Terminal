$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$firmwarePath = Join-Path $root 'Makefile/CM7/build_B/BaseProject_CM7_B.bin'
foreach ($resumeOffset in @(0, 8192)) {
$job = Start-Job -ArgumentList $firmwarePath,$resumeOffset -ScriptBlock {
    param($firmwarePath, $resumeOffset)
    $ErrorActionPreference = 'Stop'
    $image = [IO.File]::ReadAllBytes($firmwarePath)
    $offset = $resumeOffset
    $begun = $resumeOffset -gt 0
    $listener = [Net.HttpListener]::new()
    $listener.Prefixes.Add('http://127.0.0.1:18757/')
    $listener.Start()
    try {
        'READY'
        while ($true) {
            $pending = $listener.GetContextAsync()
            if (-not $pending.Wait(15000)) { throw '上传请求超时。' }
            $context = $pending.Result
            $request = $context.Request
            $body = 'OK'
            $done = $false
            switch ($request.Url.AbsolutePath) {
                '/ota/status' { $body = "busy=$([int]$begun)`nother_core_busy=0`nrunning=0`n" }
                '/ota/begin' {
                    if ($request.Headers['X-Fw-Size'] -ne "$($image.Length)" -or
                        $request.Headers['X-Fw-Base'] -ne '0x080A0000' -or
                        $request.Headers['X-Ota-Core'] -ne 'cm7') { throw 'begin 元数据错误。' }
                    $begun = $true
                    $body = "OTA ready written=$offset"
                }
                '/ota/chunk' {
                    if (-not $begun -or [int]$request.Headers['X-Offset'] -ne $offset) { throw 'offset 错误。' }
                    $buffer = [IO.MemoryStream]::new()
                    try {
                        $request.InputStream.CopyTo($buffer)
                        $bytes = $buffer.ToArray()
                        if ($bytes.Length -eq 0 -or $bytes.Length -gt 8192) { throw '块长度错误。' }
                        for ($i = 0; $i -lt $bytes.Length; $i++) {
                            if ($bytes[$i] -ne $image[$offset + $i]) { throw '二进制数据不一致。' }
                        }
                        $offset += $bytes.Length
                    } finally { $buffer.Dispose() }
                }
                '/ota/end' {
                    if ($offset -ne $image.Length) { throw '镜像未完整上传。' }
                    $done = $true
                    $body = 'OTA OK, rebooting'
                }
                default { throw '未知路由。' }
            }
            $data = [Text.Encoding]::UTF8.GetBytes($body)
            $context.Response.ContentLength64 = $data.Length
            $context.Response.OutputStream.Write($data, 0, $data.Length)
            $context.Response.Close()
            if ($done) { "UPLOADER PASS bytes=$offset"; break }
        }
    } finally { $listener.Stop(); $listener.Close() }
}
try {
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        if (@(Receive-Job $job -Keep) -contains 'READY') { $ready = $true; break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw '模拟 HTTP 服务启动失败。' }
    & (Join-Path $root 'ota_upload.ps1') -Device 'http://127.0.0.1:18757' -Core cm7 -Firmware $firmwarePath -Version 101
    $null = Wait-Job $job -Timeout 10
    $result = @(Receive-Job $job)
    $result
    if (-not ($result -match 'UPLOADER PASS')) { throw '上传集成测试失败。' }
    if ([LocalOtaCrc32]::Compute([Text.Encoding]::ASCII.GetBytes('123456789')) -ne [uint32]3421780262) {
        throw 'CRC32 标准向量失败。'
    }
    'CRC32 PASS'
} finally { Stop-Job $job; Remove-Job $job }
}
