$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = 'D:/OpenCV'
$mingw = 'D:/QT/Tools/mingw530_32/bin'
$env:PATH = "$mingw;$env:PATH"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
New-Item -ItemType Directory -Force $root | Out-Null
$source = Join-Path $root 'opencv-3.4.20'
$cmakeRoot = Join-Path $root 'cmake-3.27.9-windows-x86_64'
if (!(Test-Path "$cmakeRoot\bin\cmake.exe")) {
    $archive = Join-Path $root 'cmake.zip'
    Invoke-WebRequest -UseBasicParsing 'https://github.com/Kitware/CMake/releases/download/v3.27.9/cmake-3.27.9-windows-x86_64.zip' -OutFile $archive
    Expand-Archive $archive $root -Force
    Remove-Item $archive
}
if (!(Test-Path "$source\CMakeLists.txt")) {
    $archive = Join-Path $root 'opencv.zip'
    Invoke-WebRequest -UseBasicParsing 'https://github.com/opencv/opencv/archive/refs/tags/3.4.20.zip' -OutFile $archive
    Expand-Archive $archive $root -Force
    Remove-Item $archive
}
$cmake = "$cmakeRoot\bin\cmake.exe"
& $cmake -S $source -B "$root\build-mingw32" -G 'MinGW Makefiles' "-DCMAKE_MAKE_PROGRAM=$mingw/mingw32-make.exe" "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" "-DCMAKE_INSTALL_PREFIX=$root/mingw32" -DCMAKE_BUILD_TYPE=Release '-DBUILD_LIST=core,imgproc,objdetect' -DBUILD_SHARED_LIBS=ON -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF -DWITH_IPP=OFF -DWITH_OPENCL=OFF -DWITH_OPENEXR=OFF -DWITH_FFMPEG=OFF -DWITH_MSMF=OFF -DWITH_QT=OFF -DWITH_PROTOBUF=OFF -DENABLE_PRECOMPILED_HEADERS=OFF -DCPU_BASELINE=SSE2 -DCPU_DISPATCH= -DOPENCV_ENABLE_ALLOCATOR_STATS=OFF
if ($LASTEXITCODE) { throw 'OpenCV configure failed' }
& $cmake -S $source -B "$root\build-mingw32" -DOPENCV_INSTALL_BINARIES_PREFIX=x86/mingw/
if ($LASTEXITCODE) { throw 'OpenCV install layout configure failed' }
& $cmake --build "$root\build-mingw32" --parallel 6
if ($LASTEXITCODE) { throw 'OpenCV build failed' }
& $cmake --install "$root\build-mingw32"
if ($LASTEXITCODE) { throw 'OpenCV install failed' }
Write-Output "OpenCV installed in $root\mingw32"
