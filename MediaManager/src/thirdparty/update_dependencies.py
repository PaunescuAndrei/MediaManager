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

# MODEL_URL = "https://github.com/mosynthkey/beat_this_cpp/raw/main/onnx/beat_this.onnx"
# # Path to src/resources/models/beat_this.onnx. Script is in src/thirdparty/
# MODEL_DEST_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "models"))
# MODEL_DEST = os.path.join(MODEL_DEST_DIR, "beat_this.onnx")

# BEAT_THIS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "beat_this")
# BEAT_THIS_FILES = [
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/beat_this_api.h",
#         "path": "beat_this_api.h"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/beat_this_api.cpp",
#         "path": "beat_this_api.cpp"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/InferenceProcessor.h",
#         "path": "InferenceProcessor.h"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/InferenceProcessor.cpp",
#         "path": "InferenceProcessor.cpp"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/MelSpectrogram.h",
#         "path": "MelSpectrogram.h"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/MelSpectrogram.cpp",
#         "path": "MelSpectrogram.cpp"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/Postprocessor.h",
#         "path": "Postprocessor.h"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/Postprocessor.cpp",
#         "path": "Postprocessor.cpp"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h",
#         "path": "miniaudio.h"
#     },
#     {
#         "url": "https://raw.githubusercontent.com/mreineck/pocketfft/cpp/pocketfft_hdronly.h",
#         "path": "pocketfft_hdronly.h"
#     }
# ]

def download_file(url, path):
    print(f"Downloading {os.path.basename(path)}...")
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as response:
            content = response.read()
            with open(path, 'wb') as f:
                f.write(content)
        return True
    except Exception as e:
        print(f"Error downloading {path}: {e}")
        return False

# def update_beat_this():
#     print(f"Updating BeatThis dependencies in {BEAT_THIS_DIR}...")
#     if not os.path.exists(BEAT_THIS_DIR):
#         os.makedirs(BEAT_THIS_DIR)
        
#     success_count = 0
#     for file_info in BEAT_THIS_FILES:
#         full_path = os.path.join(BEAT_THIS_DIR, file_info["path"])
#         if download_file(file_info["url"], full_path):
#             success_count += 1
            
#     print(f"BeatThis update complete. {success_count}/{len(BEAT_THIS_FILES)} files processed.")

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
    print(f"Updating AI Model (beat_this.onnx)...")
    if not os.path.exists(MODEL_DEST_DIR):
        os.makedirs(MODEL_DEST_DIR)
    
    if download_file(MODEL_URL, MODEL_DEST):
        print(f"Model downloaded to {MODEL_DEST}")
    else:
        print("Failed to download AI Model.")

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
