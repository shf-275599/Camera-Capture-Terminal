<#
只读设备测试：检查主页、multipart、多帧 JPEG 解码、并发状态查询和断线后重连。
在 Windows PowerShell 执行：./tests/http/mjpeg_test.ps1 -Device 192.168.1.10
不执行 OTA，不擦写或复位设备。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Device,
    [ValidateRange(2, 100)][int]$Frames = 5
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$baseUri = 'http://{0}' -f $Device
$homeResponse = Invoke-WebRequest -Uri "$baseUri/" -UseBasicParsing -TimeoutSec 10
if ($homeResponse.Content -notmatch '<img src="/stream"') { throw '主页缺少 /stream 图像' }

function Read-LineBytes([System.IO.Stream]$Stream) {
    $line = New-Object System.Collections.Generic.List[byte]
    while ($true) {
        $value = $Stream.ReadByte()
        if ($value -lt 0) { throw '推流提前结束' }
        if ($value -eq 10) { break }
        if ($line.Count -ge 1024) { throw 'multipart 行过长' }
        $line.Add([byte]$value)
    }
    return [System.Text.Encoding]::ASCII.GetString($line.ToArray()).TrimEnd([char]13)
}

function Test-Stream([int]$Count) {
    $request = [System.Net.HttpWebRequest]::Create("$baseUri/stream")
    $request.Proxy = $null
    $request.Timeout = 10000
    $request.ReadWriteTimeout = 10000
    $response = $null
    $stream = $null
    try {
        $response = $request.GetResponse()
        if ($response.ContentType -notmatch '^multipart/x-mixed-replace;\s*boundary=frame$') {
            throw "响应类型错误：$($response.ContentType)"
        }
        $stream = $response.GetResponseStream()
        # 长连接保持打开时，主 HTTP 任务必须仍能响应。
        $status = Invoke-WebRequest -Uri "$baseUri/status" -UseBasicParsing -TimeoutSec 10
        if ($status.StatusCode -ne 200) { throw '并发状态查询失败' }
        for ($index = 0; $index -lt $Count; $index++) {
            if ((Read-LineBytes $stream) -ne '--frame') { throw 'boundary 错误' }
            $length = 0
            $jpegType = $false
            while ($true) {
                $line = Read-LineBytes $stream
                if ($line -eq '') { break }
                if ($line -match '^Content-Length: (\d+)$') { $length = [int]$Matches[1] }
                if ($line -eq 'Content-Type: image/jpeg') { $jpegType = $true }
            }
            if (!$jpegType -or $length -lt 4 -or $length -gt 524288) { throw '帧头无效' }
            $bytes = New-Object byte[] $length
            $offset = 0
            while ($offset -lt $length) {
                $read = $stream.Read($bytes, $offset, $length - $offset)
                if ($read -eq 0) { throw 'JPEG 数据被截断' }
                $offset += $read
            }
            if ($bytes[0] -ne 255 -or $bytes[1] -ne 216 -or
                $bytes[$length - 2] -ne 255 -or $bytes[$length - 1] -ne 217) { throw 'JPEG 标记错误' }
            $memory = New-Object System.IO.MemoryStream(,$bytes)
            $image = $null
            try {
                $image = [System.Drawing.Image]::FromStream($memory, $true, $true)
                if ($image.Width -ne 800 -or $image.Height -ne 480) { throw 'JPEG 尺寸错误' }
                Write-Host "PASS JPEG frame=$index bytes=$length size=800x480"
            }
            finally {
                if ($image) { $image.Dispose() }
                $memory.Dispose()
            }
            if ((Read-LineBytes $stream) -ne '') { throw '帧尾 CRLF 错误' }
        }
    }
    finally {
        if ($stream) { $stream.Dispose() }
        if ($response) { $response.Dispose() }
        $request.Abort()
    }
}

Test-Stream $Frames
# 设备端检测发送失败后会回收单观看连接；允许短暂 503，再验证新连接。
$reconnected = $false
for ($attempt = 0; $attempt -lt 5; $attempt++) {
    Start-Sleep -Milliseconds 1200
    try {
        Test-Stream 2
        $reconnected = $true
        break
    }
    catch [System.Net.WebException] {
        if (!$_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 503) { throw }
    }
}
if (!$reconnected) { throw '断线后无法重新建立推流连接' }
Write-Host 'PASS MJPEG：主页、多帧解码、并发状态查询、重连；LCD 实际画面需现场确认'
