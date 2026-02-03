# Beat This! Integration

This folder contains the source code for the "Beat This!" native C++ beat detection library, along with its dependencies (`miniaudio` and `pocketfft`).

## Automated Update

To update the source files to the latest versions from their respective repositories, run the Python script provided in this folder:

```bash
python update_dependencies.py
```

This script will:
1.  Download the latest C++ source files for `beat_this`.
2.  Download the latest `miniaudio.h` and `pocketfft_hdronly.h`.

## Manual Update Instructions

If the script fails or you prefer manual updates:

1.  **Beat This Sources:** Download `.h` and `.cpp` files from [mosynthkey/beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp/tree/main/Source).
    *   **Important:** You must manually comment out `#define MINIAUDIO_IMPLEMENTATION` in `beat_this_api.cpp`.
2.  **miniaudio:** Download `miniaudio.h` from [mackron/miniaudio](https://github.com/mackron/miniaudio).
3.  **pocketfft:** Download `pocketfft_hdronly.h` from [mreineck/pocketfft](https://github.com/mreineck/pocketfft/tree/cpp).

## External Binaries (Not handled by script)

These components are large binary files and are **not** updated by the script. Update them manually if needed.

### ONNX Runtime
*   **Location:** `src/thirdparty/onnxruntime/`
*   **Version:** 1.18.0 (Recommended)
*   **Update:** Download the Windows x64 zip from [ONNX Runtime Releases](https://github.com/microsoft/onnxruntime/releases). Replace the `include` and `lib` folders.

### AI Model
*   **Location:** `src/resources/models/beat_this.onnx`
*   **Update:** Download `beat_this.onnx` from [mosynthkey/beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp/tree/main/onnx).
