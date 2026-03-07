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