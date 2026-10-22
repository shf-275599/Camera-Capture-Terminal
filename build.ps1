$ErrorActionPreference = 'Stop'
$qtBin = 'D:\QT\5.11.1\mingw53_32\bin'
$mingwBin = 'D:\QT\Tools\mingw530_32\bin'
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path build)) { New-Item build -ItemType Directory | Out-Null }
    Push-Location build
    try {
        & "$qtBin\qmake.exe" ../src/qt-ota.pro
        if ($LASTEXITCODE) { throw 'qmake 失败。' }
        & "$mingwBin\mingw32-make.exe" -j4
        if ($LASTEXITCODE) { throw '编译失败。' }
    } finally { Pop-Location }
    Copy-Item build/release/qt-ota.exe ./qt-ota.exe -Force
    & "$qtBin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler ./qt-ota.exe
    if ($LASTEXITCODE) { throw 'Qt 部署失败。' }
    foreach ($dll in @('libgcc_s_dw2-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        Copy-Item (Join-Path $mingwBin $dll) . -Force
    }
} finally { Pop-Location }
