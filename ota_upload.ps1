[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Device,
    [Parameter(Mandatory = $true)][ValidateSet('cm7', 'cm4')][string]$Core,
    [Parameter(Mandatory = $true)][string]$Firmware,
    [Parameter(Mandatory = $true)][uint32]$Version
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
if (-not ('LocalOtaCrc32' -as [type])) {
    Add-Type -TypeDefinition @'
public static class LocalOtaCrc32 {
    public static uint Compute(byte[] data) {
        uint crc = 0xffffffffU;
        foreach (byte b in data) {
            crc ^= b;
            for (int i = 0; i < 8; i++)
                crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320U : 0U);
        }
        return ~crc;
    }
}
'@
}

$image = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Firmware).Path)
if ($image.Length -lt 8 -or $image.Length -gt 393216) { throw '镜像长度必须为 8～393216 字节。' }
if ($Device -notmatch '^https?://') { $Device = "http://$Device" }
$baseUri = $Device.TrimEnd('/')
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$handler.AllowAutoRedirect = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(45)
$client.DefaultRequestHeaders.ExpectContinue = $false

function Send-OtaRequest {
    param([string]$Method, [string]$Route, [hashtable]$Headers, [byte[]]$Body = @())
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($Method), "$baseUri$Route")
    $response = $null
    try {
        $request.Headers.ConnectionClose = $true
        foreach ($name in $Headers.Keys) { [void]$request.Headers.TryAddWithoutValidation($name, [string]$Headers[$name]) }
        if ($Method -eq 'POST') {
            $request.Content = [System.Net.Http.ByteArrayContent]::new($Body)
            $request.Content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
        }
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) { throw "HTTP $([int]$response.StatusCode): $text" }
        return $text
    }
    finally {
        if ($null -ne $response) { $response.Dispose() }
        $request.Dispose()
    }
}

$begun = $false
$ending = $false
try {
    $statusText = Send-OtaRequest 'GET' '/ota/status' @{ 'X-Ota-Core' = $Core }
    $status = @{}
    foreach ($line in ($statusText -split "`r?`n")) {
        if ($line -match '^([^=]+)=(.*)$') { $status[$matches[1]] = $matches[2] }
    }
    if ($status['other_core_busy'] -ne '0') { throw '设备另一核已有 OTA 会话。' }
    if ($status['running'] -notin @('0', '1')) { throw '设备实际运行槽不可用。' }
    $target = 1 - [int]$status['running']
    $bases = if ($Core -eq 'cm7') { @([uint32]0x08020000, [uint32]0x080A0000) } else { @([uint32]0x08120000, [uint32]0x081A0000) }
    $targetBase = $bases[$target]
    $entry = [BitConverter]::ToUInt32($image, 4)
    if (($entry -band 1) -ne 1 -or $entry -lt $targetBase -or $entry -ge ([uint64]$targetBase + $image.Length)) {
        throw ('镜像入口不属于目标槽，请使用链接到 0x{0:X8} 的 {1} BIN。' -f $targetBase, $Core)
    }
    $crc = [LocalOtaCrc32]::Compute($image)
    Write-Host ('目标 {0} 槽 {1}，地址 0x{2:X8}，长度 {3}，CRC32 0x{4:X8}' -f $Core, $target, $targetBase, $image.Length, $crc)
    # 同一镜像重复 begin 返回当前偏移；响应丢失时保留会话供重跑续传。
    $begun = $true
    $beginReply = Send-OtaRequest 'POST' '/ota/begin' @{
        'X-Ota-Core' = $Core; 'X-Fw-Size' = $image.Length; 'X-Fw-Version' = $Version
        'X-Fw-Crc32' = ('0x{0:X8}' -f $crc); 'X-Fw-Base' = ('0x{0:X8}' -f $targetBase)
    }
    if ($beginReply -match '\bwritten=(\d+)') {
        $offset = [uint32]::Parse($matches[1])
    } elseif ($status['busy'] -eq '0' -and $beginReply.Trim() -eq 'OTA ready') {
        $offset = [uint32]0 # 兼容尚未部署续传功能的旧固件首次上传。
    } else { throw 'begin 未返回续传偏移。' }
    if ($offset -gt $image.Length) { throw '设备返回的续传偏移超出镜像长度。' }
    Write-Host "从偏移 $offset 继续上传。"
    for (; $offset -lt $image.Length;) {
        $count = [Math]::Min(8192, $image.Length - $offset)
        $chunk = [byte[]]::new($count)
        [Array]::Copy($image, $offset, $chunk, 0, $count)
        $null = Send-OtaRequest 'POST' '/ota/chunk' @{ 'X-Offset' = $offset } $chunk
        $offset += $count
        Write-Progress -Activity '上传固件' -Status "$offset / $($image.Length)" -PercentComplete ($offset * 100 / $image.Length)
    }
    $ending = $true
    $result = Send-OtaRequest 'POST' '/ota/end' @{}
    Write-Progress -Activity '上传固件' -Completed
    Write-Host $result
    Write-Host '上传及 pending 保存成功；设备正在复位，请核对启动槽、版本和实际业务表现。'
}
catch {
    if ($begun -and -not $ending) {
        Write-Warning '上传未完成；可在空闲 60 秒、会话总计 10 分钟内重跑同一命令续传，超时后会话自动清理。'
    }
    if ($ending) { Write-Warning 'end 阶段结果未确认，请先查询设备启动日志和 OTA 状态，避免直接重传。' }
    throw
}
finally { $client.Dispose() }
