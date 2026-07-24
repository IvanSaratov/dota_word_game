$ErrorActionPreference = "Stop"

$modelDir = Join-Path $PSScriptRoot "..\assets\models"
New-Item -ItemType Directory -Force -Path $modelDir | Out-Null

$files = @(
  @{
    Name = "en_PP-OCRv5_rec_mobile_infer.onnx"
    Url = "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.5.0/onnx/PP-OCRv5/rec/en_PP-OCRv5_rec_mobile_infer.onnx"
    Sha256 = "c3461add59bb4323ecba96a492ab75e06dda42467c9e3d0c18db5d1d21924be8"
  },
  @{
    Name = "ppocrv5_en_dict.txt"
    Url = "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.5.0/paddle/PP-OCRv5/rec/en_PP-OCRv5_rec_mobile_infer/ppocrv5_en_dict.txt"
    Sha256 = "e025a66d31f327ba0c232e03f407ae8d105e1e709e7ccb3f408aa778c24e70d6"
  }
)

foreach ($file in $files) {
  $destination = Join-Path $modelDir $file.Name
  Invoke-WebRequest -Uri $file.Url -OutFile $destination
  $actual = (Get-FileHash -Algorithm SHA256 $destination).Hash.ToLowerInvariant()
  if ($actual -ne $file.Sha256) {
    Remove-Item $destination
    throw "Checksum mismatch for $($file.Name): $actual"
  }
}
