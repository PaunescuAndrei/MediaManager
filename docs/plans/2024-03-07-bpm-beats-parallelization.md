# BPM and Beats Parallelization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the massive memory usage and VRAM leaks caused by repeated PyTorch model loading, and implement automatic lazy loading and unloading of the `beat_this.pt` model.

**Architecture:** Create a `SharedModelManager` class that uses a `std::weak_ptr` to track the model and issues `std::shared_ptr`s to requesting threads. The model will be dynamically loaded when needed and automatically unloaded (destroyed) when the last active task's `shared_ptr` goes out of scope. Both `CalculateBpmRunnable` and `mascotsAnimationsThread` will be refactored to fetch temporary shared pointers from the manager instead of keeping their own permanent or redundant instances.

**Tech Stack:** C++, Qt (`QThreadPool`, `QtConcurrent`, `QFuture`), LibTorch, Smart Pointers (`std::shared_ptr`, `std::weak_ptr`)

---

### Task 1: Create SharedModelManager

**Files:**
- Create: `MediaManager/src/general/SharedModelManager.h`

**Step 1: Implement the Manager Class**

Create a new file `SharedModelManager.h` with thread-safe lazy-loading logic:
```cpp
#pragma once
#include <mutex>
#include <memory>
#include <QString>
#include <QCoreApplication>
#include "beat_this_api.h"
#include "definitions.h"
#include <QDebug>

class SharedModelManager {
private:
    std::mutex mtx;
    std::weak_ptr<BeatThis::BeatThis> weak_model;

public:
    std::shared_ptr<BeatThis::BeatThis> getModel() {
        std::lock_guard<std::mutex> lock(mtx);
        
        // Try to promote weak_ptr to shared_ptr
        if (auto shared = weak_model.lock()) {
            return shared; // Model is already loaded
        }
        
        // Model is not loaded (or was unloaded). Load it now.
        try {
            QString modelPath = QCoreApplication::applicationDirPath() + "/" + MODELS_PATH + "/beat_this.pt";
            auto shared = std::make_shared<BeatThis::BeatThis>(modelPath.toStdString());
            weak_model = shared; // Store reference for future callers
            qDebug() << "BeatThis model loaded into memory.";
            return shared;
        } catch (const std::exception& e) {
            qDebug() << "Failed to initialize BeatThis model: " << e.what();
            return nullptr;
        }
    }
};
```

**Step 2: Commit**
```bash
git add MediaManager/src/general/SharedModelManager.h
git commit -m "feat: create SharedModelManager for automatic model loading/unloading"
```

---

### Task 2: Integrate SharedModelManager into MainApp

**Files:**
- Modify: `MediaManager/src/QtClasses/MainApp.h`
- Modify: `MediaManager/src/QtClasses/MainApp.cpp`

**Step 1: Add Manager to MainApp**
In `MainApp.h`, include the new header and add a member variable:
```cpp
#include "SharedModelManager.h"

// Inside MainApp class:
SharedModelManager* beatModelManager = nullptr;
```

**Step 2: Initialize Manager**
In `MainApp.cpp` constructor, instantiate the manager before any components that might use it:
```cpp
this->beatModelManager = new SharedModelManager();
```
And in `MainApp::~MainApp()` clean it up:
```cpp
delete this->beatModelManager;
```

**Step 3: Commit**
```bash
git add MediaManager/src/QtClasses/MainApp.h MediaManager/src/QtClasses/MainApp.cpp
git commit -m "feat: integrate SharedModelManager into MainApp"
```

---

### Task 3: Refactor CalculateBpmRunnable

**Files:**
- Modify: `MediaManager/src/general/CalculateBpmRunnable.cpp`

**Step 1: Remove Redundant Model Instantiation**
In `CalculateBpmRunnable::run()`, completely remove the `std::make_unique<BeatThis::BeatThis>` logic that loads a new model per thread.

**Step 2: Use the Shared Model**
Instead, fetch the shared pointer at the start of `run()`:
```cpp
// Add include at top: #include "MainApp.h"

void CalculateBpmRunnable::run() {
    auto beatAnalyzer = qMainApp->beatModelManager->getModel();
    if (!beatAnalyzer) {
        return; // Failed to load model
    }

    while (!this->queue->isEmpty() && this->manager->isRunning()) {
        // ... (existing item popping loop) ...
        
        // Instead of beatAnalyzer->process_audio, it just uses the shared pointer:
        BeatThis::BeatResult result = beatAnalyzer->process_audio(audioData, sampleRate, channels);
        
        // ... (existing processing) ...
    }
    // 'beatAnalyzer' goes out of scope here.
}
```

**Step 3: Commit**
```bash
git add MediaManager/src/general/CalculateBpmRunnable.cpp
git commit -m "refactor: use shared lazily-loaded model in CalculateBpmRunnable"
```

---

### Task 4: Refactor mascotsAnimationsThread

**Files:**
- Modify: `MediaManager/src/general/mascotsAnimationsThread.h`
- Modify: `MediaManager/src/general/mascotsAnimationsThread.cpp`

**Step 1: Remove Permanent Model from Header**
In `mascotsAnimationsThread.h`, remove `BeatThis::BeatThis* beatAnalyzer = nullptr;`

**Step 2: Clean Constructor and Destructor**
In `mascotsAnimationsThread.cpp`, remove the initialization of `this->beatAnalyzer` inside the constructor.
Also remove the `delete this->beatAnalyzer;` logic from the destructor.

**Step 3: Update `update_beats` and `rebuildBeatsCache`**
In `update_beats(...)`, retrieve a local shared pointer instead of using `this->beatAnalyzer`:
```cpp
void mascotsAnimationsThread::update_beats(QString track_path, bool cache_check) {
    // ...
    if (cached_beats) return;

    auto analyzer = this->App->beatModelManager->getModel();
    if (!analyzer) return;

    // ...
    if (utils::readAudioFile(...)) {
        auto result = analyzer->process_audio(audioData, sampleRate, channels);
        // ...
    }
    // 'analyzer' shared_ptr goes out of scope here.
}
```

Do the same inside `rebuildBeatsCache()`: fetch `auto analyzer = this->App->beatModelManager->getModel();` at the beginning of the function or inside the processing loop if parallelized.

**Step 4: Commit**
```bash
git add MediaManager/src/general/mascotsAnimationsThread.h MediaManager/src/general/mascotsAnimationsThread.cpp
git commit -m "refactor: lazy load and automatically unload model in mascotsAnimationsThread"
```

---

### Task 5: Parallelize Beats Cache Rebuild

**Files:**
- Modify: `MediaManager/src/general/mascotsAnimationsThread.cpp`

**Step 1: Ensure Directory Exists at App Startup**
Move `QDir().mkpath(BEATS_CACHE_PATH);` out of the processing loops and into `MainApp::createMissingDirs()` to avoid race conditions.

**Step 2: Refactor `rebuildBeatsCache` with QtConcurrent**
Instead of a sequential loop, use `QtConcurrent::run` to process audio files in parallel:
```cpp
void mascotsAnimationsThread::rebuildBeatsCache() {
    QStringList files = utils::getFilesQt(this->App->musicPlayer->trackPlaylistFolder, true);
    
    // Fetch a shared pointer so the model stays loaded while tasks are created/running
    auto sharedAnalyzer = this->App->beatModelManager->getModel();
    if (!sharedAnalyzer) return;

    QList<QFuture<void>> futures;

    for (const QString& trackpath : files) {
        futures.append(QtConcurrent::run([this, trackpath, sharedAnalyzer]() {
            if (!this->running) return;

            // ... (mime type checking, check if cache exists) ...

            if (utils::readAudioFile(trackpath, audioData, sampleRate, channels)) {
                try {
                    // Use sharedAnalyzer here
                    auto result = sharedAnalyzer->process_audio(audioData, sampleRate, channels);
                    // ... write cache file
                } catch (...) {}
            }
        }));
    }

    // Optionally, wait for futures to finish or manage them
    for (auto& future : futures) {
        future.waitForFinished();
    }
}
```

**Step 3: Commit**
```bash
git add MediaManager/src/general/mascotsAnimationsThread.cpp
git commit -m "feat: process beats cache rebuild in parallel"
```