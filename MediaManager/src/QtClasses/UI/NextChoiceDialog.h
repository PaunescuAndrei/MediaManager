#pragma once
#include <QDialog>
#include "ui_NextChoiceDialog.h"
#include "definitions.h"
#include <QIcon>
#include <QHash>
#include <functional>

class VideoPreviewWidget;
class QLabel;

class NextChoiceDialog : public QDialog
{
    Q_OBJECT
public:
    using CandidateBuilder = std::function<QList<NextVideoChoice>(int count)>;
    using VoidFunc = std::function<void()>;
    using IntAccessor = std::function<int()>;
    using IntMutator = std::function<void(int)>;

    explicit NextChoiceDialog(QWidget* parent = nullptr);
    ~NextChoiceDialog();
    void setChoices(const QList<NextVideoChoice>& newChoices);
    NextVideoChoice selectedChoice() const;
    void setCounterLabel(QLabel* label);
    void updateRefreshButtonText();
    void setStarIcons(QIcon* activeIcon, QIcon* halfIcon, QIcon* inactiveIcon) {
        this->activeIcon = activeIcon;
        this->halfIcon = halfIcon;
        this->inactiveIcon = inactiveIcon;
    }
    void setAppendMode(bool enabled);
    bool isAppendMode() const { return this->appendMode; }
    void toggleAppendMode();
    void setTogglingEnabled(bool enabled);

    void setup(CandidateBuilder builder, int requestedChoices,
               VoidFunc onDecrementCounter, IntAccessor onGetRefreshCounter,
               IntMutator onSetRefreshCounter,
               IntAccessor onGetRollCounter, IntMutator onSetRollCounter,
               const QList<NextVideoChoice>& initialCandidates);
private:
    void rebuildCards(int startIndex = 0);
    QWidget* buildCard(const NextVideoChoice& choice, int index);
    void applySelection(int index);
    void startPreviewForIndex(int index);
    void stopPreviewForIndex(int index);
    void stopAllPreviews();
    void rebuildForCurrentMode();
    void removeExcessCards(int keepCount);
    void resizeToFit();
    QList<NextVideoChoice> choices;
    Ui::NextChoiceDialog ui;
    int currentIndex = -1;
    QIcon* activeIcon = nullptr;
    QIcon* halfIcon = nullptr;
    QIcon* inactiveIcon = nullptr;
    QTimer bringToFrontTimer;
    QHash<int, VideoPreviewWidget*> previewWidgets;
    bool previewAutoplayAllMute = false;
    bool previewEnabled = true;
    QLabel* counterLabel = nullptr;
    bool appendMode = false;
    bool togglingEnabled = true;

    CandidateBuilder candidateBuilder;
    int requestedChoicesCount = 1;
    VoidFunc onDecrementCounter;
    IntAccessor onGetRefreshCounter;
    IntMutator onSetRefreshCounter;
    IntAccessor onGetRollCounter;
    IntMutator onSetRollCounter;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
};
