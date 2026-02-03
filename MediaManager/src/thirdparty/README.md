# Third-Party Dependencies

This directory contains external libraries and dependencies used by MediaManager.

## Automated Dependency Management

To download and update all third-party dependencies (including headers, sources, and large binaries like ONNX Runtime), run the Python automation script:

```bash
python update_dependencies.py
```

This script will automatically:
1.  **Beat This!**: Download the latest C++ source files and dependencies (`miniaudio`, `pocketfft`).
2.  **ONNX Runtime**: Download and extract the correct version of ONNX Runtime (currently v1.18.0) for Windows x64.

## Manual Setup Instructions

If the script fails or you are on a restricted environment, follow these steps:

### 1. Beat This! Library
*   **Location**: `beat_this/`
*   **Sources**: Download `.h` and `.cpp` files from [mosynthkey/beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp/tree/main/Source).
    *   *Note*: The script handles patching `beat_this_api.cpp` if necessary (e.g. `MINIAUDIO_IMPLEMENTATION`).
*   **Dependencies**:
    *   `miniaudio.h` from [mackron/miniaudio](https://github.com/mackron/miniaudio)
    *   `pocketfft_hdronly.h` from [mreineck/pocketfft](https://github.com/mreineck/pocketfft/tree/cpp)

### 2. ONNX Runtime
*   **Location**: `onnxruntime/`
*   **Version**: 1.23.2 (Required)
*   **Download**: Get the Windows x64 zip from [ONNX Runtime Releases](https://github.com/microsoft/onnxruntime/releases).
*   **Install**: Extract the zip and place the `include` and `lib` folders into `MediaManager/src/thirdparty/onnxruntime/`.

### 3. AI Model (Required)
*   **Location**: `MediaManager/src/resources/models/beat_this.onnx`
*   **Download**: Get `beat_this.onnx` from [mosynthkey/beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp/tree/main/onnx).
*   **Install**: Place the file at the location above. (This file is ignored by git due to size).
