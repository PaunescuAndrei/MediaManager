# MediaManager

MediaManager is a feature-rich desktop application built with C++ and Qt 6 for organizing, managing, and playing local video and audio collections. It features advanced filtering, weighted random playback, AI-powered analysis, and extensive UI customization including animated mascots.

## Features

- **Media Library Management**: SQLite-backed database to track video paths, play counts, ratings, and watched status.
- **Advanced Playback Modes**: 
  - Weighted random shuffle based on user preferences.
  - "Smart Next" suggestions (sequential, random, or series-based).
  - External player integration (MPC-HC) and internal Qt-based audio/video players.
- **AI Integration**: Built-in "Beat This" engine using ONNX Runtime for audio beat detection and analysis.
- **Visual Previews**: 
  - Animated video thumbnails on hover.
  - Automatic thumbnail generation using `mtn`.
- **Customizable UI**:
  - Animated desktop mascots (system tray and overlay integration).
  - Dynamic layouts and themes.
  - Taskbar progress and state integration.
- **Utilities**: 
  - File tagging and categorization.
  - Playlist import/export.
  - Automatic backup of database and configuration.

## Prerequisites

To build MediaManager, you need the following tools installed:

1.  **Visual Studio 2022** (with "Desktop development with C++" workload).
2.  **Qt 6.x** (MSVC 2019/2022 64-bit).
    *   *Note: Ensure the Qt Visual Studio Tools extension is installed.*
3.  **Python 3.x** (required for dependency automation scripts).
4.  **vcpkg** (C++ Package Manager) integrated with Visual Studio.

## Dependencies

MediaManager relies on several third-party libraries. Some are managed via `vcpkg`, while others are handled by an included Python script.

### 1. Install Vcpkg Libraries

Open a terminal (PowerShell or CMD) and install the following libraries using `vcpkg`. Ensure you use the `x64-windows` triplet.

```powershell
vcpkg install opencv4 libzippp libzip zlib bzip2 --triplet x64-windows
```

*Ensure vcpkg is integrated into Visual Studio by running `vcpkg integrate install` if you haven't done so previously.*

### 2. Fetch External Dependencies

A Python script is provided to download specific dependencies that are not in vcpkg or require specific versions/patching (Beat This! sources, ONNX Runtime binaries, and AI models).

Run the following command from the project root:

```bash
python MediaManager/src/thirdparty/update_dependencies.py
```

This will download and place the following into `MediaManager/src/`:
- **ONNX Runtime (v1.23.2)**: Headers and DLLs.
- **Beat This!**: Source code and API headers.
- **beat_this.onnx**: The AI model file required for beat detection.

## Build Instructions

1.  **Open the Solution**:
    Open `MediaManager.sln` in Visual Studio 2022.

2.  **Select Configuration**:
    Choose **ReleaseDebug** or **Release** configuration and **x64** platform.
    *   *Note: `ReleaseDebug` is the recommended configuration for development/testing.*

3.  **Build**:
    Build the solution (Ctrl+Shift+B).
    *   Visual Studio should automatically link the vcpkg libraries and the Qt modules.

4.  **Run**:
    The executable will be output to:
    `MediaManager/x64/ReleaseDebug/MediaManager.exe`

## Project Structure

- **src/general/**: Core logic (Database, Player interfaces, Config, Utilities).
- **src/QtClasses/**: UI implementation (Windows, Dialogs, Models, Widgets).
  - **Models/**: `VideosModel` and proxy models for filtering/sorting.
  - **UI/**: Dialog implementations and `.ui` forms.
  - **Widgets/**: Custom widgets like the video previewer and mascot renderers.
- **src/thirdparty/**: External libraries (Beat This, ONNX Runtime, etc.).
- **src/resources/**: Assets (Icons, Models, Defaults).

