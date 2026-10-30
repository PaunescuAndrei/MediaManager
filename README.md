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
  - Animated mascots in two in-window QML panels (mirroring, side-directory flipping, beat-driven swaps).
  - Dynamic layouts and themes.
  - Taskbar progress and state integration.
- **Utilities**: 
  - File tagging and categorization.
  - Playlist import/export.
  - Automatic backup of database and configuration.

## Mascot Panels

The two mascot panels are Qt Quick surfaces hosted in the main window (`MascotSurface` +
`MascotImageProvider`), driven by `MascotController`. The controller picks the image, the
prefetcher (`MascotPrefetcher` + `MascotImageCache`) decodes it off the GUI thread, and the
panels only render what they are handed. Images are scaled to the panel height with the aspect
ratio kept, and each image is centred on its visible content (its alpha-weighted centre) rather
than on its geometric middle.

Settings live in `config.ini`. All but three of them also have a control in the settings dialog:
`mascots_media`, `mascots_image_cache_mb` and `mascots_image_cache_max_items` are file-only (the
cache bounds are applied at startup and again when the dialog is applied, so a hand-edited value
needs the dialog's Apply or a restart).

| Key | Meaning |
| --- | --- |
| `mascots` | Show the two panels. |
| `mascots_media` | Media kind. `Images` is the only kind this build renders; any other value logs a warning and falls back to images. |
| `mascots_width_percent` | Width of each panel as a percentage of the window width (default `15`). |
| `mascots_mirror` | Both panels show the same image, the right one mirrored. |
| `mascots_center_content` | Centre on the image's visible content (`True`) or on its geometric middle (`False`). |
| `mascots_allfiles_random` | Pick from every file under the mascots directory instead of one subdirectory at a time. |
| `mascots_color_theme` | Recolour the window's highlight from the palette of the image on screen. |
| `mascots_animated` | Let the beat loop swap and flip the panels. |
| `mascots_frequency` | Milliseconds between beat checks (default `400`). |
| `mascots_random_change` / `mascots_random_chance` | Whether a beat may swap, and the chance in percent (default `10`). |
| `mascots_use_seed` | Deterministic picks; combined with `random_use_seed` and `random_seed`. |
| `mascots_image_cache_mb` / `mascots_image_cache_max_items` | Decoded-image cache bounds (default `128` MB and `12` items; values below 16 MB / 2 items are clamped). |

A file whose parent directory is `right` is drawn mirrored on the left panel, and one from `left`
is drawn mirrored on the right panel.

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
- **ONNX Runtime (v1.24.3)**: Headers and DLLs.
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
    `x64/ReleaseDebug/MediaManager.exe`

## Project Structure

- **src/general/**: Core logic (Database, Player interfaces, Config, Utilities) and the mascot
  media pipeline (`MascotPrefetcher`, `MascotImageCache`, `mascotMediaUtils`).
- **src/QtClasses/**: UI implementation (Windows, Dialogs, Models, Widgets).
  - **Models/**: `VideosModel` and proxy models for filtering/sorting.
  - **UI/**: Dialog implementations and `.ui` forms.
  - **Widgets/**: Custom widgets, including the video previewer and the mascot panel
    (`MascotSurface`) with its image provider (`MascotImageProvider`).
- **src/qml/**: QML used by the video preview popup and the mascot panels.
- **src/thirdparty/**: External libraries (Beat This, ONNX Runtime, etc.).
- **src/resources/**: Assets (Icons, Models, Defaults).

