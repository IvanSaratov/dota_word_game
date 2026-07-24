# PP-OCRv5 recognition assets

Run the pinned downloader from the repository root:

```powershell
pwsh -File scripts/fetch-models.ps1
```

It downloads the English PP-OCRv5 mobile recognition model and its UTF-8
character dictionary from the RapidOCR v3.5.0 model set:

| File | SHA-256 |
| --- | --- |
| `en_PP-OCRv5_rec_mobile_infer.onnx` | `c3461add59bb4323ecba96a492ab75e06dda42467c9e3d0c18db5d1d21924be8` |
| `ppocrv5_en_dict.txt` | `e025a66d31f327ba0c232e03f407ae8d105e1e709e7ccb3f408aa778c24e70d6` |

The model originates from PaddleOCR's PP-OCRv5 project and is distributed in
ONNX form by RapidOCR. PaddleOCR and the RapidOCR model repository identify
these artifacts as Apache-2.0 licensed. The downloaded files are intentionally
ignored by Git; this directory contains only provenance and reproducibility
metadata.
