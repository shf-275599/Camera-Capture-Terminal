param(
    [string]$Device = '192.168.10.2',
    [Parameter(Mandatory = $true)][ValidateSet('cm7', 'cm4')][string]$Core,
    [Parameter(Mandatory = $true)][string]$Firmware,
    [Parameter(Mandatory = $true)][uint32]$Version
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
if (-not ('ResumeTestCrc32' -as [type])) {
    Add-Type -TypeDefinition @'
public static class ResumeTestCrc32 {
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
if ($image.Length -lt 16384) { throw '测试镜像至少需要 16384 字节。' }
$handler = [Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$client = [Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(45)
$client.DefaultRequestHeaders.ExpectContinue = $false
function Request {
    param([string]$Method, [string]$Path, [hashtable]$Headers, [byte[]]$Body = @(), [int]$Code = 200)
    $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method), "http://$Device$Path")
    $response = $null
    try {
        $request.Headers.ConnectionClose = $true
        foreach ($key in $Headers.Keys) { [void]$request.Headers.TryAddWithoutValidation($key, [string]$Headers[$key]) }
        if ($Method -eq 'POST') { $request.Content = [Net.Http.ByteArrayContent]::new($Body) }
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if ([int]$response.StatusCode -ne $Code) { throw "$Path expected=$Code actual=$([int]$response.StatusCode) $text" }
        return $text
    } finally { if ($response) { $response.Dispose() }; $request.Dispose() }
}
function Disconnect-Chunk {
    param([uint32]$Offset, [int]$Count)
    $tcp = [Net.Sockets.TcpClient]::new($Device, 80)
    try {
        $stream = $tcp.GetStream()
        $header = [Text.Encoding]::ASCII.GetBytes("POST /ota/chunk HTTP/1.1`r`nHost: device`r`nContent-Length: 8192`r`nX-Offset: $Offset`r`nConnection: close`r`n`r`n")
        $stream.Write($header, 0, $header.Length)
        $stream.Write($image, $Offset, $Count)
        # 不读取响应；用 FIN 结束发送，保证已发送字节可被服务端接收。
        $tcp.Client.Shutdown([Net.Sockets.SocketShutdown]::Send)
    } finally { $tcp.Dispose() }
}
try {
    $status = Request GET '/ota/status' @{'X-Ota-Core'=$Core}
    if ($status -notmatch '(?m)^busy=0\r?$' -or $status -notmatch '(?m)^other_core_busy=0\r?$') { throw '测试要求无现存会话。' }
    if ($status -notmatch '(?m)^target_base=(0x[0-9A-Fa-f]+)\r?$') { throw '缺少目标地址。' }
    $base = [Convert]::ToUInt32($matches[1].Substring(2), 16)
    $entry = [BitConverter]::ToUInt32($image, 4)
    if (($entry -band 1) -ne 1 -or $entry -lt $base -or $entry -ge ([uint64]$base + $image.Length)) { throw '测试镜像链接地址错误。' }
    $headers = @{'X-Ota-Core'=$Core; 'X-Fw-Size'="$($image.Length)"; 'X-Fw-Version'="$Version";
        'X-Fw-Crc32'=('0x{0:X8}' -f [ResumeTestCrc32]::Compute($image)); 'X-Fw-Base'=('0x{0:X8}' -f $base)}
    $null = Request POST '/ota/begin' $headers
    Disconnect-Chunk 0 3
    $resume = Request POST '/ota/begin' $headers
    if ($resume -notmatch 'written=0\b') { throw '半块断线推进了偏移。' }
    "RESUME PASS $Core incomplete body kept offset=0"
    foreach ($key in @('X-Ota-Core','X-Fw-Size','X-Fw-Version','X-Fw-Crc32','X-Fw-Base')) {
        $wrong = $headers.Clone()
        $wrong[$key] = if ($key -eq 'X-Ota-Core') { if ($Core -eq 'cm7') { 'cm4' } else { 'cm7' } } else { '1' }
        $null = Request POST '/ota/begin' $wrong @() 409
    }
    "RESUME PASS $Core mismatched core/size/version/crc/base rejected"
    $chunk = [byte[]]::new(8192)
    [Array]::Copy($image, 0, $chunk, 0, $chunk.Length)
    $null = Request POST '/ota/chunk' @{'X-Offset'='0'} $chunk
    Disconnect-Chunk 8192 8192
    $resume = Request POST '/ota/begin' $headers
    if ($resume -notmatch 'written=16384\b') { throw "完整块响应未读后偏移错误：$resume" }
    "RESUME PASS $Core unread response kept offset=16384"
    # 重跑正式上传入口，必须从 16384 继续，最终 Flash CRC 和 pending 保存由设备检查。
    & (Join-Path $PSScriptRoot '../../ota_upload.ps1') -Device $Device -Core $Core -Firmware $Firmware -Version $Version
} finally { $client.Dispose() }
