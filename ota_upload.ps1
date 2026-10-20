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
    if ($status['busy'] -ne '0' -or $status['other_core_busy'] -ne '0') { throw '设备已有 OTA 会话。' }
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
    # begin 的响应也可能丢失，失败时尝试取消已创建的会话。
    $begun = $true
    $null = Send-OtaRequest 'POST' '/ota/begin' @{
        'X-Ota-Core' = $Core; 'X-Fw-Size' = $image.Length; 'X-Fw-Version' = $Version
        'X-Fw-Crc32' = ('0x{0:X8}' -f $crc); 'X-Fw-Base' = ('0x{0:X8}' -f $targetBase)
    }
    for ($offset = 0; $offset -lt $image.Length;) {
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
        try { $null = Send-OtaRequest 'POST' '/ota/abort' @{} } catch { Write-Warning '取消请求未确认，设备会话将按超时清理。' }
    }
    if ($ending) { Write-Warning 'end 阶段结果未确认，请先查询设备启动日志和 OTA 状态，避免直接重传。' }
    throw
}
finally { $client.Dispose() }
