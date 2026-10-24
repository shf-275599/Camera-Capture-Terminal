$ErrorActionPreference = 'Stop'
$qtBin = 'D:\QT\5.11.1\mingw53_32\bin'
$mingwBin = 'D:\QT\Tools\mingw530_32\bin'
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path build)) { New-Item build -ItemType Directory | Out-Null }
    Push-Location build
    try {
        & "$qtBin\qmake.exe" ../src/qt-camare.pro
        if ($LASTEXITCODE) { throw 'qmake failed' }
        & "$mingwBin\mingw32-make.exe" -j4
        if ($LASTEXITCODE) { throw 'Build failed' }
    } finally { Pop-Location }
    Copy-Item build/release/qt-camare.exe ./qt-camare.exe -Force
    & "$qtBin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler ./qt-camare.exe
    if ($LASTEXITCODE) { throw 'Deployment failed' }
    foreach ($dll in @('libgcc_s_dw2-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        Copy-Item (Join-Path $mingwBin $dll) . -Force
    }
    Copy-Item 'D:/OpenCV/mingw32/x86/mingw/bin/*.dll' . -Force
    New-Item models -ItemType Directory -Force | Out-Null
    Copy-Item 'D:/OpenCV/opencv-3.4.20/data/haarcascades/haarcascade_frontalface_default.xml' models/ -Force
} finally { Pop-Location }
