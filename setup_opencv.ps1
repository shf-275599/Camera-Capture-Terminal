$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = 'D:/OpenCV'
$mingw = 'D:/QT/Tools/mingw530_32/bin'
$env:PATH = "$mingw;$env:PATH"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
New-Item -ItemType Directory -Force $root | Out-Null
$source = Join-Path $root 'opencv-4.10.0'
$install = Join-Path $root 'mingw32-4.10.0'
$cmakeRoot = Join-Path $root 'cmake-3.27.9-windows-x86_64'
if (!(Test-Path "$cmakeRoot\bin\cmake.exe")) {
    $archive = Join-Path $root 'cmake.zip'
    Invoke-WebRequest -UseBasicParsing 'https://github.com/Kitware/CMake/releases/download/v3.27.9/cmake-3.27.9-windows-x86_64.zip' -OutFile $archive
    Expand-Archive $archive $root -Force
    Remove-Item $archive
}
if (!(Test-Path "$source\CMakeLists.txt")) {
    $archive = Join-Path $root 'opencv-4.10.0.zip'
    Invoke-WebRequest -UseBasicParsing 'https://github.com/opencv/opencv/archive/refs/tags/4.10.0.zip' -OutFile $archive
    Expand-Archive $archive $root -Force
    Remove-Item $archive
}
$cmake = "$cmakeRoot\bin\cmake.exe"
$mutexHeader = (Resolve-Path (Join-Path $source '3rdparty/protobuf/src/google/protobuf/stubs/mutex.h')).Path
$mutexText = [IO.File]::ReadAllText($mutexHeader)
$mutexText = $mutexText.Replace('constexpr WrappedMutex() {}', 'constexpr WrappedMutex() = default;')
$mutexText = $mutexText.Replace('  WrappedMutex() {}', '  constexpr WrappedMutex() = default;')
[IO.File]::WriteAllText($mutexHeader, $mutexText, (New-Object Text.UTF8Encoding $false))
$env:CXXFLAGS = '-mstackrealign'
$env:CFLAGS = '-mstackrealign'
& $cmake -S $source -B "$root\build-mingw32-4.10.0" -G 'MinGW Makefiles' "-DCMAKE_MAKE_PROGRAM=$mingw/mingw32-make.exe" "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" "-DCMAKE_INSTALL_PREFIX=$install" -DCMAKE_BUILD_TYPE=Release '-DBUILD_LIST=core,imgproc,objdetect,dnn' -DBUILD_SHARED_LIBS=ON -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF -DWITH_IPP=OFF -DWITH_OPENCL=OFF -DWITH_OPENEXR=OFF -DWITH_FFMPEG=OFF -DWITH_MSMF=OFF -DWITH_QT=OFF -DWITH_PROTOBUF=ON -DBUILD_PROTOBUF=ON -DWITH_OPENVINO=OFF -DWITH_ITT=OFF -DWITH_ADE=OFF -DENABLE_PRECOMPILED_HEADERS=OFF -DCPU_BASELINE=SSE2 -DCPU_DISPATCH= -DOPENCV_ENABLE_ALLOCATOR_STATS=OFF -DOPENCV_INSTALL_BINARIES_PREFIX=x86/mingw/ -DOPENCV_DNN_OPENCL=OFF
if ($LASTEXITCODE) { throw 'OpenCV configure failed' }
& $cmake -S $source -B "$root\build-mingw32-4.10.0" -DCMAKE_CXX_FLAGS=-mstackrealign -DCMAKE_C_FLAGS=-mstackrealign
if ($LASTEXITCODE) { throw 'OpenCV stack alignment configure failed' }
& $cmake --build "$root\build-mingw32-4.10.0" --parallel 4
if ($LASTEXITCODE) { throw 'OpenCV build failed' }
& $cmake --install "$root\build-mingw32-4.10.0"
if ($LASTEXITCODE) { throw 'OpenCV install failed' }
$modelDir = Join-Path $install 'models'
New-Item -ItemType Directory -Force $modelDir | Out-Null
$model = Join-Path $modelDir 'face_detection_yunet_2023mar.onnx'
$modelHash = '8F2383E4DD3CFBB4553EA8718107FC0423210DC964F9F4280604804ED2552FA4'
if (!(Test-Path $model) -or (Get-FileHash $model -Algorithm SHA256).Hash -ne $modelHash) {
    Invoke-WebRequest -UseBasicParsing 'https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx' -OutFile $model
}
if ((Get-FileHash $model -Algorithm SHA256).Hash -ne $modelHash) { throw 'YuNet model checksum mismatch' }
Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/opencv/opencv_zoo/main/models/face_detection_yunet/LICENSE' -OutFile (Join-Path $modelDir 'LICENSE-YuNet.txt')
Write-Output "OpenCV and YuNet installed in $install"
