param([string]$Device = '192.168.10.2')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$h = [Net.Http.HttpClientHandler]::new()
$h.UseProxy = $false
$c = [Net.Http.HttpClient]::new($h)
$c.Timeout = [TimeSpan]::FromSeconds(30)
$c.DefaultRequestHeaders.ExpectContinue = $false
function Check-Request {
    param([string]$Method, [string]$Path, [hashtable]$Headers, [byte[]]$Body, [int]$Code)
    $r = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method), "http://$Device$Path")
    $p = $null
    try {
        foreach ($key in $Headers.Keys) { [void]$r.Headers.TryAddWithoutValidation($key, [string]$Headers[$key]) }
        if ($Method -eq 'POST') { $r.Content = [Net.Http.ByteArrayContent]::new($Body) }
        $p = $c.SendAsync($r).GetAwaiter().GetResult()
        $text = $p.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if ([int]$p.StatusCode -ne $Code) { throw "$Path expected=$Code actual=$([int]$p.StatusCode) $text" }
        Write-Host "PASS $Method $Path HTTP $Code"
        return $text
    } finally { if ($p) { $p.Dispose() }; $r.Dispose() }
}
try {
    $null = Check-Request GET '/status' @{} @() 200
    $null = Check-Request POST '/status' @{} @() 405
    $null = Check-Request GET '/unknown' @{} @() 404
    $null = Check-Request POST '/ota/begin' @{'X-Ota-Core'='invalid'} @() 400
    $status = Check-Request GET '/ota/status' @{'X-Ota-Core'='cm7'} @() 200
    if ($status -notmatch '(?m)^running=0\r?$' -or $status -notmatch '(?m)^flags=0x00\r?$') {
        throw '本测试要求 CM7 已确认运行 A，且无 pending。'
    }
    $headers = @{'X-Ota-Core'='cm7';'X-Fw-Size'='8';'X-Fw-Version'='999';'X-Fw-Crc32'='0';'X-Fw-Base'='0x08020000'}
    $null = Check-Request POST '/ota/begin' $headers @() 409
    $headers['X-Fw-Base'] = '0x080A0000'
    $headers['X-Fw-Size'] = '393217'
    $null = Check-Request POST '/ota/begin' $headers @() 413
    $headers['X-Fw-Size'] = '8'
    $null = Check-Request POST '/ota/begin' $headers @() 200
    $null = Check-Request POST '/ota/begin' $headers @() 409
    $null = Check-Request POST '/ota/chunk' @{'X-Offset'='1'} ([byte[]](1,2)) 409
    $tcp = [Net.Sockets.TcpClient]::new($Device, 80)
    try {
        $stream = $tcp.GetStream()
        $stream.ReadTimeout = 5000
        $head = [Text.Encoding]::ASCII.GetBytes("POST /ota/chunk HTTP/1.1`r`nHost: device`r`nContent-Length: 8193`r`nX-Offset: 0`r`n`r`n")
        $stream.Write($head, 0, $head.Length)
        $reply = [byte[]]::new(512)
        $count = $stream.Read($reply, 0, $reply.Length)
        if ([Text.Encoding]::ASCII.GetString($reply, 0, $count) -notmatch '^HTTP/1.1 413 ') { throw '超限请求未返回 413。' }
        'PASS oversized header HTTP 413'
    } finally { $tcp.Dispose() }
    $null = Check-Request POST '/ota/chunk' @{'X-Offset'='0'} ([byte[]](1,2)) 409
    $null = Check-Request POST '/ota/begin' $headers @() 200
    $null = Check-Request POST '/ota/chunk' @{'X-Offset'='0'} ([byte[]](1,2,3,4,5,6,7,8)) 200
    $null = Check-Request POST '/ota/end' @{} @() 500
    $null = Check-Request POST '/ota/abort' @{} @() 200
    $status = Check-Request GET '/ota/status' @{'X-Ota-Core'='cm7'} @() 200
    if ($status -notmatch '(?m)^busy=0\r?$' -or $status -notmatch '(?m)^flags=0x00\r?$') {
        throw '错误后状态未清理。'
    }
    'DEVICE NEGATIVE TEST PASS: bad CRC does not set pending'
} finally { $c.Dispose() }
