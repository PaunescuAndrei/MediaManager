# Media Manager

Simple media manager built with qt with random features for me to practice or play with.

## Build Instructions

### Prerequisites
1.  **Visual Studio 2022** (with C++ Desktop Development workload).
2.  **Qt 6.x** (MSVC 2019/2022 64-bit kit).
3.  **Python 3.x** (for dependency management).

### Setting up Dependencies
Before building, you must download the required third-party libraries (including ONNX Runtime and BeatThis sources).

Run the automation script from the project root:

```bash
python MediaManager/src/thirdparty/update_dependencies.py
```

This will populate `MediaManager/src/thirdparty/` with the necessary headers and binaries.

### Building
Open `MediaManager.sln` in Visual Studio and build the **ReleaseDebug** or **Release** configuration.
