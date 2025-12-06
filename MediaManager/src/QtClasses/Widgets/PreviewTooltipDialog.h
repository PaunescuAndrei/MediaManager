#pragma once
#include <QDialog>

class MainWindow;
class VideoPreviewWidget;

// Tooltip-style popup that hosts a video preview and mirrors main preview controls.
class PreviewTooltipDialog : public QDialog
{
public:
    explicit PreviewTooltipDialog(MainWindow* owner, QWidget* parent = nullptr);
    void setPreviewWidget(VideoPreviewWidget* widget);
    VideoPreviewWidget* previewWidget() const { return this->preview; }
    qint64 positionForResume() const;

    QString path;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void closeEvent(QCloseEvent* e) override;

private:
    MainWindow* mw = nullptr;
    VideoPreviewWidget* preview = nullptr;
};
