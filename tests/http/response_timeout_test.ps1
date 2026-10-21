# 仅用于临时 OTA_RESPONSE_FAULT_TEST 固件；正常固件不提供 X-Test-Drop-Reply。
param([string]$Device = '192.168.10.2', [string]$Port = 'COM8', [switch]$TotalOnly)
$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port,115200,'None',8,'One')
$log = [Text.StringBuilder]::new()
function Read-Log {
    $text = $serial.ReadExisting()
    if ($text) { [void]$log.Append($text); Write-Output $text }
}
function Request {
    param([string]$Method, [string]$Path, [hashtable]$Headers, [byte[]]$Body = @(), [switch]$ExpectDisconnect)
    & {
        $tcp = [Net.Sockets.TcpClient]::new($Device,80)
        try {
            $stream = $tcp.GetStream()
            $stream.ReadTimeout = 40000
            $head = "$Method $Path HTTP/1.1`r`nHost: device`r`nConnection: close`r`nContent-Length: $($Body.Length)`r`n"
            foreach ($key in $Headers.Keys) { $head += "${key}: $($Headers[$key])`r`n" }
            $bytes = [Text.Encoding]::ASCII.GetBytes($head + "`r`n")
            $stream.Write($bytes,0,$bytes.Length)
            if ($Body.Length) { $stream.Write($Body,0,$Body.Length) }
            $buffer = [byte[]]::new(1024)
            $reply = [IO.MemoryStream]::new()
            try {
                while (($count = $stream.Read($buffer,0,$buffer.Length)) -gt 0) { $reply.Write($buffer,0,$count) }
                if ($ExpectDisconnect) {
                    if ($reply.Length -ne 0) { throw 'Expected EOF without HTTP response' }
                    return
                }
                $text = [Text.Encoding]::ASCII.GetString($reply.ToArray())
                if ($text -notmatch '^HTTP/1.1 200 ') { throw "Unexpected response: $text" }
                return $text.Substring($text.IndexOf("`r`n`r`n") + 4)
            } finally { $reply.Dispose() }
        } finally { $tcp.Dispose() }
    }
}
try {
    if ([IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) { throw 'Serial port unavailable' }
    $serial.Open()
    if (-not $TotalOnly) {
    foreach ($core in @('cm7','cm4')) {
        $status = Request GET '/ota/status' @{'X-Ota-Core'=$core}
        if ($status -notmatch '(?m)^busy=0\r?$' -or $status -notmatch '(?m)^other_core_busy=0\r?$') { throw 'Device busy' }
        if ($status -notmatch '(?m)^target_base=(0x[0-9A-Fa-f]+)\r?$') { throw 'Missing target base' }
        $headers = @{'X-Ota-Core'=$core;'X-Fw-Size'='32768';'X-Fw-Version'='9599';'X-Fw-Crc32'='0';'X-Fw-Base'=$matches[1]}
        $drop = $headers.Clone()
        $drop['X-Test-Drop-Reply'] = '1'
        Request POST '/ota/begin' $drop -ExpectDisconnect
        $resume = Request POST '/ota/begin' $headers
        if ($resume -notmatch 'written=0\b') { throw 'Begin response loss destroyed session' }
        Request POST '/ota/chunk' @{'X-Offset'='0';'X-Test-Drop-Reply'='1'} ([byte[]]::new(8192)) -ExpectDisconnect
        $resume = Request POST '/ota/begin' $headers
        if ($resume -notmatch 'written=8192\b') { throw 'Chunk send failure destroyed progress' }
        $null = Request POST '/ota/chunk' @{'X-Offset'='8192'} ([byte[]]::new(1))
        $null = Request POST '/ota/abort' @{}
        "FAULT PASS $core begin response lost; chunk response lost; resumed offset=8192"
        Read-Log
    }
    $evidence = $log.ToString()
    foreach ($path in @('begin','chunk')) {
        if ([regex]::Matches($evidence, "SEND path=/ota/$path close=0 reply=-1").Count -lt 2) {
            throw "Missing confirmed server send failure for $path"
        }
    }
    'FAULT PASS actual server reply=-1 for begin/chunk on both cores'
    }
    $status = Request GET '/ota/status' @{'X-Ota-Core'='cm4'}
    if ($status -notmatch '(?m)^target_base=(0x[0-9A-Fa-f]+)\r?$') { throw 'Missing target base' }
    $headers = @{'X-Ota-Core'='cm4';'X-Fw-Size'='32768';'X-Fw-Version'='9598';'X-Fw-Crc32'='0';'X-Fw-Base'=$matches[1]}
    $null = Request POST '/ota/begin' $headers
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $offset = 0
    $nextWrite = 30
    'TOTAL START real 600-second session; one byte every 30 seconds prevents idle expiry'
    while ($watch.Elapsed.TotalSeconds -lt 630) {
        Read-Log
        $status = Request GET '/ota/status' @{'X-Ota-Core'='cm4'}
        $seconds = $watch.Elapsed.TotalSeconds
        if ($status -match '(?m)^busy=0\r?$') {
            if ($seconds -lt 599) { throw "Premature expiry seconds=$seconds" }
            if ($status -notmatch '(?m)^flags=0x00\r?$') { throw 'Unexpected pending' }
            Read-Log
            if ($log.ToString() -notmatch 'session timeout total=(\d+) idle=(\d+)') { throw 'Missing device timeout evidence' }
            if ([uint32]$matches[1] -lt 600000 -or [uint32]$matches[2] -ge 60000) { throw 'Wrong timeout cause' }
            "TOTAL PASS elapsed=$([Math]::Round($seconds,1)) device_total=$($matches[1]) device_idle=$($matches[2]) written_before_expiry=$offset"
            return
        }
        if ($seconds -ge $nextWrite -and $seconds -lt 599) {
            $null = Request POST '/ota/chunk' @{'X-Offset'="$offset"} ([byte[]](0))
            $offset++
            $nextWrite += 30
            "TOTAL ACTIVE seconds=$([int]$seconds) written=$offset"
        }
        Start-Sleep -Seconds 2
    }
    throw 'Total timeout did not occur'
} finally { if ($serial.IsOpen) { $serial.Close() } }
