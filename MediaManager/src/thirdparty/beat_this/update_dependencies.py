import os
import urllib.request
import re

# Configuration
FILES_TO_DOWNLOAD = [
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
    print(f"Downloading {path} from {url}...")
    try:
        with urllib.request.urlopen(url) as response:
            content = response.read()
            with open(path, 'wb') as f:
                f.write(content)
        print(f"Successfully downloaded {path}")
        return True
    except Exception as e:
        print(f"Error downloading {path}: {e}")
        return False

def main():
    print("Starting dependency update...")
    
    # Ensure we are in the correct directory (where the script is)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    
    success_count = 0
    for file_info in FILES_TO_DOWNLOAD:
        if download_file(file_info["url"], file_info["path"]):
            success_count += 1
    
    print(f"\nUpdate complete. {success_count}/{len(FILES_TO_DOWNLOAD)} files processed.")
    print("Note: ONNX Runtime and Model files must be updated manually if versions change.")

if __name__ == "__main__":
    main()
