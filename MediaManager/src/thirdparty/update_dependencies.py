import os
import urllib.request
import zipfile
import shutil
import ssl

# Allow legacy SSL if needed
ssl._create_default_https_context = ssl._create_unverified_context

# Configuration
ONNX_VERSION = "1.18.0"
ONNX_URL = f"https://github.com/microsoft/onnxruntime/releases/download/v{ONNX_VERSION}/onnxruntime-win-x64-{ONNX_VERSION}.zip"
ONNX_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "onnxruntime")

MODEL_URL = "https://github.com/mosynthkey/beat_this_cpp/raw/main/onnx/beat_this.onnx"
# Path to src/resources/models/beat_this.onnx. Script is in src/thirdparty/
MODEL_DEST_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "models"))
MODEL_DEST = os.path.join(MODEL_DEST_DIR, "beat_this.onnx")

BEAT_THIS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "beat_this")
BEAT_THIS_FILES = [
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/beat_this_api.h",
        "path": "beat_this_api.h"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/beat_this_api.cpp",
        "path": "beat_this_api.cpp"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/InferenceProcessor.h",
        "path": "InferenceProcessor.h"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/InferenceProcessor.cpp",
        "path": "InferenceProcessor.cpp"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/MelSpectrogram.h",
        "path": "MelSpectrogram.h"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/MelSpectrogram.cpp",
        "path": "MelSpectrogram.cpp"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/Postprocessor.h",
        "path": "Postprocessor.h"
    },
    {
        "url": "https://raw.githubusercontent.com/mosynthkey/beat_this_cpp/main/Source/Postprocessor.cpp",
        "path": "Postprocessor.cpp"
    },
    {
        "url": "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h",
        "path": "miniaudio.h"
    },
    {
        "url": "https://raw.githubusercontent.com/mreineck/pocketfft/cpp/pocketfft_hdronly.h",
        "path": "pocketfft_hdronly.h"
    }
]

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
    print(f"Updating BeatThis dependencies in {BEAT_THIS_DIR}...")
    if not os.path.exists(BEAT_THIS_DIR):
        os.makedirs(BEAT_THIS_DIR)
        
    success_count = 0
    for file_info in BEAT_THIS_FILES:
        full_path = os.path.join(BEAT_THIS_DIR, file_info["path"])
        if download_file(file_info["url"], full_path):
            success_count += 1
            
    print(f"BeatThis update complete. {success_count}/{len(BEAT_THIS_FILES)} files processed.")

def update_onnxruntime():
    print(f"Updating ONNX Runtime {ONNX_VERSION}...")
    zip_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "onnxruntime.zip")
    
    # Download
    if download_file(ONNX_URL, zip_path):
        print("Extracting ONNX Runtime...")
        try:
            # Clear existing directory if it exists, but keep the directory itself
            if os.path.exists(ONNX_DIR):
                shutil.rmtree(ONNX_DIR)
            os.makedirs(ONNX_DIR)

            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                # The zip usually has a root folder like "onnxruntime-win-x64-1.18.0"
                # We want to strip that and put contents directly into ONNX_DIR
                root_folder = zip_ref.namelist()[0].split('/')[0]
                
                for member in zip_ref.namelist():
                    if member.startswith(root_folder + "/include/") or member.startswith(root_folder + "/lib/"):
                        # Extract and strip root folder
                        target_path = os.path.join(ONNX_DIR, member[len(root_folder)+1:])
                        
                        # Skip directories
                        if member.endswith('/'):
                            if not os.path.exists(target_path) and target_path.strip():
                                os.makedirs(target_path)
                            continue
                            
                        # Ensure dir exists
                        os.makedirs(os.path.dirname(target_path), exist_ok=True)
                        
                        # Read and write file
                        with zip_ref.open(member) as source, open(target_path, "wb") as target:
                            shutil.copyfileobj(source, target)
                            
            print(f"ONNX Runtime extracted to {ONNX_DIR}")
            
            # Create a version file
            with open(os.path.join(ONNX_DIR, "VERSION_NUMBER"), "w") as f:
                f.write(ONNX_VERSION)
                
        except Exception as e:
            print(f"Error extracting ONNX Runtime: {e}")
        finally:
            if os.path.exists(zip_path):
                os.remove(zip_path)
    else:
        print("Failed to download ONNX Runtime.")

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
    update_onnxruntime()
    print("-" * 30)
    update_model()
    print("\nAll tasks completed.")

if __name__ == "__main__":
    main()
