#pragma once
#include <QDialog>
#include "ui_NextChoiceDialog.h"
#include "definitions.h"
#include <QIcon>
#include <QHash>

class VideoPreviewWidget;

class NextChoiceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NextChoiceDialog(QWidget* parent = nullptr);
    ~NextChoiceDialog();
    void setChoices(const QList<NextVideoChoice>& newChoices);
    NextVideoChoice selectedChoice() const;
    void setTitle(const QString& text);
    void setStarIcons(QIcon* activeIcon, QIcon* halfIcon, QIcon* inactiveIcon) {
        this->activeIcon = activeIcon;
        this->halfIcon = halfIcon;
        this->inactiveIcon = inactiveIcon;
    }
private:
    void rebuildCards();
    QWidget* buildCard(const NextVideoChoice& choice, int index);
    void applySelection(int index);
    void startPreviewForIndex(int index);
    void stopPreviewForIndex(int index);
    void stopAllPreviews();
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
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
};
