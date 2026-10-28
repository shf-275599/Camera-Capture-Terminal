$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$modelDir = 'D:/OpenCV/mingw32-4.10.0/models'
New-Item -ItemType Directory -Force $modelDir | Out-Null
$model = Join-Path $modelDir 'facial_expression_recognition_mobilefacenet_2022july.onnx'
$hash = '4F61307602FC089CE20488A31D4E4614E3C9753A7D6C41578C854858B183E1A9'
if (!(Test-Path $model) -or (Get-FileHash $model -Algorithm SHA256).Hash -ne $hash) {
    Invoke-WebRequest -UseBasicParsing 'https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/facial_expression_recognition/facial_expression_recognition_mobilefacenet_2022july.onnx' -OutFile $model
}
if ((Get-FileHash $model -Algorithm SHA256).Hash -ne $hash) { throw 'Expression model checksum mismatch' }
Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/opencv/opencv_zoo/main/LICENSE' -OutFile (Join-Path $modelDir 'LICENSE-OpenCV-Zoo.txt')
Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/opencv/opencv_zoo/main/models/facial_expression_recognition/README.md' -OutFile (Join-Path $modelDir 'README-expression.md')
Write-Output 'Expression model downloaded and verified'
