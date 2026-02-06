#include "stdafx.h"
#include "CalculateBpmRunnable.h"
#include "CalculateBpmManager.h"
#include "beat_this_api.h"
#include "utils.h"
#include "definitions.h"
#include <QCoreApplication>
#include <QDebug>
#include <numeric>

CalculateBpmRunnable::CalculateBpmRunnable(NonBlockingQueue<BpmWorkItem>* queue, CalculateBpmManager* manager, QObject* parent)
    : QObject(parent), QRunnable(), queue(queue), manager(manager), cancelFlag(nullptr) {
}

CalculateBpmRunnable::~CalculateBpmRunnable() {
}

void CalculateBpmRunnable::setCancelFlag(std::atomic<bool>* flag) {
    this->cancelFlag = flag;
}

void CalculateBpmRunnable::clearCancelFlag() {
    this->cancelFlag = nullptr;
}

void CalculateBpmRunnable::run() {
    QString modelPath = QCoreApplication::applicationDirPath() + "/" + MODELS_PATH + "/beat_this.onnx";
    std::unique_ptr<BeatThis::BeatThis> beatAnalyzer;

    try {
        beatAnalyzer = std::make_unique<BeatThis::BeatThis>(modelPath.toStdString());
    }
    catch (const std::exception& e) {
        qDebug() << "Failed to initialize BeatThis in Runnable: " << e.what();
        return;
    }

    while (!this->queue->isEmpty() && this->manager->isRunning()) {
        std::optional<BpmWorkItem> itemOpt = this->queue->pop();
        if (itemOpt) {
            BpmWorkItem item = itemOpt.value();
            
            if (!this->manager->isRunning()) {
                break;
            }

            // Check cancellation before processing
            if (this->cancelFlag && this->cancelFlag->load()) {
                break;
            }

            std::vector<float> audioData;
            int sampleRate = 0;
            int channels = 0;

            // Read audio file with cancellation support
            if (utils::readAudioFile(item.path, audioData, sampleRate, channels, this->cancelFlag)) {
                // Check cancellation after audio read
                if (this->cancelFlag && this->cancelFlag->load()) {
                    break;
                }

                try {
                    BeatThis::BeatResult result = beatAnalyzer->process_audio(audioData, sampleRate, channels);
                    
                    // Check cancellation after BeatThis processing
                    if (this->cancelFlag && this->cancelFlag->load()) {
                        break;
                    }
                    
                    std::vector<float>& beats = result.beats;
                    double bpm = -1.0;

                    if (beats.size() > 1) {
                        std::vector<double> intervals;
                        for (size_t i = 1; i < beats.size(); ++i) {
                            intervals.push_back(beats[i] - beats[i - 1]);
                        }

                        double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
                        double avgInterval = sum / intervals.size();

                        if (avgInterval > 0) {
                            bpm = 60.0 / avgInterval;
                        }
                    }

                    if (bpm > 0) {
                        emit bpmCalculated(item.id, bpm);
                    }

                }
                catch (const std::exception& e) {
                    qDebug() << "Error calculating BPM for" << item.path << ":" << e.what();
                }
            }

            if (this->manager) {
                this->manager->remove_active_id(item.id);
                this->manager->add_work_count(-1);
            }
        }
    }
}
