# Replace ONNX Runtime with LibTorch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the existing ONNX Runtime integration with LibTorch (with CUDA support) for beat detection inference in the `beat_this` third-party library.

**Architecture:** 
1. The ONNX model will need to be converted to a TorchScript (`.pt`) model (this plan assumes the `.pt` model is available or handles the C++ side assuming it's available).
2. `BeatThis::Impl` will initialize a `torch::jit::script::Module` instead of `Ort::Session`.
3. `InferenceProcessor` will be updated to take the `torch::jit::script::Module` and run inference using `torch::Tensor` inputs instead of `Ort::Value`.
4. The project build system (MSBuild/Visual Studio) will need to be updated to link against LibTorch instead of ONNX Runtime.

**Tech Stack:** C++, LibTorch (PyTorch C++ API), CUDA

---

### Task 1: Update Dependency Script

**Files:**
- Modify: `MediaManager/src/thirdparty/update_dependencies.py`

**Step 1: Write the failing test**

Run: `python MediaManager/src/thirdparty/update_dependencies.py`
Expected: Still tries to download ONNX Runtime.

**Step 2: Write minimal implementation**

Update `update_dependencies.py` to remove `update_onnxruntime()` and add `update_libtorch()`.

```python
import os
import urllib.request
import zipfile
import shutil
import ssl

# Allow legacy SSL if needed
ssl._create_default_https_context = ssl._create_unverified_context

# Configuration
LIBTORCH_URL = "https://download.pytorch.org/libtorch/cu130/libtorch-win-shared-with-deps-2.1.0%2Bcu130.zip"
LIBTORCH_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "libtorch")

MODEL_URL = "https://github.com/mosynthkey/beat_this_cpp/raw/main/onnx/beat_this.onnx"
# Path to src/resources/models/beat_this.onnx. Script is in src/thirdparty/
MODEL_DEST_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "models"))
MODEL_DEST = os.path.join(MODEL_DEST_DIR, "beat_this.onnx")

BEAT_THIS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "beat_this")
BEAT_THIS_FILES = []

def download_file(url, path):
    print(f"Downloading {os.path.basename(path)}...")
    try:
        with urllib.request.urlopen(url) as response:
            content = response.read()
            with open(path, 'wb') as f:
                f.write(content)
        return True
    except Exception as e:
        print(f"Error downloading {path}: {e}")
        return False

def update_beat_this():
    # ... keep existing update_beat_this ...

def update_libtorch():
    print("Updating LibTorch...")
    zip_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "libtorch.zip")
    
    if download_file(LIBTORCH_URL, zip_path):
        print("Extracting LibTorch...")
        try:
            if os.path.exists(LIBTORCH_DIR):
                shutil.rmtree(LIBTORCH_DIR)
                
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                # Extract directly. The zip creates a "libtorch" folder.
                # We extract it to the parent directory of LIBTORCH_DIR
                # because the zip already contains a "libtorch" root folder.
                extract_dir = os.path.dirname(LIBTORCH_DIR)
                zip_ref.extractall(extract_dir)
                
            print(f"LibTorch extracted to {LIBTORCH_DIR}")
        except Exception as e:
            print(f"Error extracting LibTorch: {e}")
        finally:
            if os.path.exists(zip_path):
                os.remove(zip_path)
    else:
        print("Failed to download LibTorch.")

def update_model():
    # ... keep existing update_model ...

def main():
    print("Starting dependency update...")
    update_beat_this()
    print("-" * 30)
    update_libtorch()
    print("-" * 30)
    update_model()
    print("\nAll tasks completed.")

if __name__ == "__main__":
    main()
```

**Step 3: Run test to verify it passes**

Run: `python MediaManager/src/thirdparty/update_dependencies.py`
Expected: Downloads and extracts LibTorch to `MediaManager/src/thirdparty/libtorch`.

**Step 4: Commit**

```bash
git add MediaManager/src/thirdparty/update_dependencies.py
git commit -m "build: replace onnxruntime with libtorch in dependency script"
```

---

### Task 2: Update Includes and Class Members in Headers

**Files:**
- Modify: `MediaManager/src/thirdparty/beat_this/beat_this_api.h:1` (Add needed includes if any, but mostly for `BeatThis` it just needs `<memory>`)
- Modify: `MediaManager/src/thirdparty/beat_this/InferenceProcessor.h:1-59`

**Step 1: Write the failing test**

We don't have a direct unit test suite for this thirdparty library. Testing will rely on manual verification or building the project.

**Step 2: Run test to verify it fails**

Run: `msbuild MediaManager.sln /p:Configuration=ReleaseDebug /m`
Expected: Fails if we remove ONNX runtime headers.

**Step 3: Write minimal implementation**

Update `InferenceProcessor.h`:

```cpp
#ifndef INFERENCE_PROCESSOR_H
#define INFERENCE_PROCESSOR_H

#include <vector>
#include <string>
#include <torch/script.h> // Include LibTorch header

/**
 * @class InferenceProcessor
 * @brief Handles neural network inference for beat detection
 * 
 * This class processes Mel spectrograms through a trained TorchScript model to detect
 * beats and downbeats. It handles chunking of long audio sequences and
 * aggregates predictions from overlapping chunks.
 */
class InferenceProcessor {
public:
    InferenceProcessor(torch::jit::script::Module& module);

    /**
     * @brief Process a full spectrogram and return beat/downbeat logits
     * @param spectrogram Input Mel spectrogram [frames][mel_bins]
     * @return Pair of vectors containing beat and downbeat logits
     */
    std::pair<std::vector<float>, std::vector<float>> process_spectrogram(
        const std::vector<std::vector<float>>& spectrogram
    );

private:
    torch::jit::script::Module& module_;

    // Chunking parameters (must match Python implementation)
    const int chunk_size = 1500;      // Size of each chunk in frames
    const int border_size = 6;        // Border size for overlap handling
    // overlap_mode is "keep_first" in Python, processed in reverse order

    // Helper functions for chunking and aggregation
    std::vector<std::vector<std::vector<float>>> split_piece(
        const std::vector<std::vector<float>>& spect,
        int chunk_size,
        int border_size
    );

    std::pair<std::vector<float>, std::vector<float>> aggregate_prediction(
        const std::vector<std::pair<std::vector<float>, std::vector<float>>>& pred_chunks,
        const std::vector<int>& starts,
        int full_size,
        int chunk_size,
        int border_size
    );

    // Helper to run LibTorch inference on a single chunk
    std::pair<std::vector<float>, std::vector<float>> run_libtorch_inference(
        const std::vector<std::vector<float>>& chunk_spect
    );
};

#endif // INFERENCE_PROCESSOR_H
```

**Step 4: Run test to verify it passes**

N/A - compilation will fail until all parts are updated.

**Step 5: Commit**

```bash
git add MediaManager/src/thirdparty/beat_this/InferenceProcessor.h
git commit -m "refactor: update InferenceProcessor header to use LibTorch instead of ONNX Runtime"
```

---

### Task 3: Update Initialization in beat_this_api.cpp

**Files:**
- Modify: `MediaManager/src/thirdparty/beat_this/beat_this_api.cpp:1-244`

**Step 1: Write the failing test**

N/A - visual verification of compilation errors.

**Step 2: Run test to verify it fails**

Run: `msbuild MediaManager.sln /p:Configuration=ReleaseDebug /m`
Expected: C++ Compilation failures due to undefined `Ort` types.

**Step 3: Write minimal implementation**

Update `beat_this_api.cpp` to initialize LibTorch.

Find the `Impl` class:
```cpp
// Pimpl implementation
class BeatThis::Impl {
public:
    torch::jit::script::Module module;

    explicit Impl(const std::string& model_path) {
        // Check if file exists
        std::ifstream file_check(model_path);
        if (!file_check.good()) {
            throw std::runtime_error("TorchScript model file not found: " + model_path);
        }
        file_check.close();
        
        try {
            // Load the model
            module = torch::jit::load(model_path);
            
            // Move to CUDA if available
            if (torch::cuda::is_available()) {
                module.to(torch::kCUDA);
            }
        } catch (const c10::Error& e) {
            std::cerr << "LibTorch error loading the model: " << e.what() << std::endl;
            throw std::runtime_error("LibTorch error: " + std::string(e.what()));
        }
    }
};
```

Update the inference call in `process_audio`:
```cpp
        // Run Inference
        InferenceProcessor processor(pImpl->module);
        auto beat_downbeat_logits = processor.process_spectrogram(spectrogram);
```

Also remove `Ort::Exception` catch blocks:
```cpp
    } catch (const c10::Error& e) {
        std::cerr << "LibTorch error: " << e.what() << std::endl;
        throw std::runtime_error("LibTorch error: " + std::string(e.what()));
    } catch (const std::exception& e) {
```

**Step 4: Run test to verify it passes**

N/A - compilation still fails because `InferenceProcessor.cpp` is not updated.

**Step 5: Commit**

```bash
git add MediaManager/src/thirdparty/beat_this/beat_this_api.cpp
git commit -m "refactor: update BeatThis::Impl to load LibTorch TorchScript model"
```

---

### Task 4: Update Inference Implementation in InferenceProcessor.cpp

**Files:**
- Modify: `MediaManager/src/thirdparty/beat_this/InferenceProcessor.cpp:1-174`

**Step 1: Write the failing test**

N/A.

**Step 2: Run test to verify it fails**

Run: `msbuild MediaManager.sln /p:Configuration=ReleaseDebug /m`
Expected: Failures in `InferenceProcessor.cpp` missing `Ort` names.

**Step 3: Write minimal implementation**

Update `InferenceProcessor.cpp`. Change `run_onnx_inference` to `run_libtorch_inference`.

```cpp
#include "InferenceProcessor.h"
#include <algorithm>
#include <numeric>

InferenceProcessor::InferenceProcessor(torch::jit::script::Module& module)
    : module_(module) {}

// Helper to run LibTorch inference on a single chunk
std::pair<std::vector<float>, std::vector<float>> InferenceProcessor::run_libtorch_inference(
    const std::vector<std::vector<float>>& chunk_spect
) {
    int64_t batch_size = 1;
    int64_t seq_len = chunk_spect.size();
    int64_t input_size = chunk_spect[0].size();
    
    // Flatten the spectrogram for tensor creation
    size_t input_tensor_size = seq_len * input_size;
    std::vector<float> input_tensor_values(input_tensor_size);
    for(size_t i=0; i < seq_len; ++i) {
        memcpy(input_tensor_values.data() + i * input_size, chunk_spect[i].data(), input_size * sizeof(float));
    }
    
    // Create CPU tensor
    auto options = torch::TensorOptions().dtype(torch::kFloat32);
    torch::Tensor input_tensor = torch::from_blob(input_tensor_values.data(), {batch_size, seq_len, input_size}, options).clone();
    
    // Move to CUDA if available
    if (torch::cuda::is_available()) {
        input_tensor = input_tensor.to(torch::kCUDA);
    }
    
    std::vector<torch::jit::IValue> inputs;
    inputs.push_back(input_tensor);
    
    // Run inference
    auto output = module_.forward(inputs).toTuple();
    
    // Extract outputs (beat, downbeat)
    torch::Tensor beat_tensor = output->elements()[0].toTensor().cpu();
    torch::Tensor downbeat_tensor = output->elements()[1].toTensor().cpu();
    
    // Assuming shape is [1, seq_len] or [seq_len]
    size_t beat_output_size = beat_tensor.numel();
    
    std::vector<float> beat_logits(beat_tensor.data_ptr<float>(), beat_tensor.data_ptr<float>() + beat_output_size);
    std::vector<float> downbeat_logits(downbeat_tensor.data_ptr<float>(), downbeat_tensor.data_ptr<float>() + beat_output_size);

    return {beat_logits, downbeat_logits};
}
```

Update the `process_spectrogram` call loop:
```cpp
    for (const auto& chunk : chunks) {
        pred_chunks.push_back(run_libtorch_inference(chunk));
    }
```

**Step 4: Run test to verify it passes**

N/A.

**Step 5: Commit**

```bash
git add MediaManager/src/thirdparty/beat_this/InferenceProcessor.cpp
git commit -m "refactor: implement LibTorch inference logic in InferenceProcessor"
```

---

### Task 5: Update MSBuild Configuration to Link LibTorch

**Files:**
- Modify: `MediaManager.sln` / `.vcxproj` files
- Action: The `.vcxproj` file needs to be updated to remove ONNX Runtime include/library paths and add LibTorch include/library paths (especially CUDA variations).

**Step 1: Write the failing test**

Run: `msbuild MediaManager.sln /p:Configuration=ReleaseDebug /m`
Expected: Linker errors or missing header errors for `<torch/script.h>`.

**Step 2: Run test to verify it fails**

N/A.

**Step 3: Write minimal implementation**

In the `.vcxproj`:
1. Find and replace `$(ProjectDir)src\thirdparty\onnxruntime\include` with:
   - `$(ProjectDir)src\thirdparty\libtorch\include;$(ProjectDir)src\thirdparty\libtorch\include\torch\csrc\api\include`
2. Find and replace `$(ProjectDir)src\thirdparty\onnxruntime\lib` with:
   - `$(ProjectDir)src\thirdparty\libtorch\lib`
3. Find `<AdditionalDependencies>` and replace `onnxruntime.lib` with:
   - `torch.lib;torch_cpu.lib;c10.lib;torch_cuda.lib;c10_cuda.lib;fbgemm.lib;kineto.lib` (and any other necessary PyTorch static libs).
4. Update the `<PostBuildEvent>` script:
   Remove: `xcopy /Y /D "$(ProjectDir)src\thirdparty\onnxruntime\lib\onnxruntime.dll" "$(OutDir)"`
   Add: 
   ```batch
   xcopy /Y /D "$(ProjectDir)src\thirdparty\libtorch\lib\*.dll" "$(OutDir)"
   ```
5. Note: Make sure to verify the precise list of libraries required to link against LibTorch with CUDA. The list of `.dll` files in `libtorch/lib/` can be extensive and copying `*.dll` ensures all dependencies are bundled.

**Step 4: Run test to verify it passes**

Run: `msbuild MediaManager.sln /p:Configuration=ReleaseDebug /m`
Expected: Build succeeds.

**Step 5: Commit**

```bash
git add MediaManager/MediaManager.vcxproj
git commit -m "build: replace ONNX Runtime with LibTorch in project configuration"
```

---
