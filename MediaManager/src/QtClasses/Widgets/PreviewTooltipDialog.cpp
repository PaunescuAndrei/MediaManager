#include "stdafx.h"
#include "PreviewTooltipDialog.h"
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QMouseEvent>
#include "VideoPreviewWidget.h"
#include "MainWindow.h"
#include "MainApp.h"
#include "config.h"

PreviewTooltipDialog::PreviewTooltipDialog(MainWindow* owner, QWidget* parent)
    : QDialog(parent), mw(owner)
{
    this->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
}

void PreviewTooltipDialog::setPreviewWidget(VideoPreviewWidget* widget)
{
    if (!widget)
        return;
    this->preview = widget;
    widget->setParent(this);
    widget->installEventFilter(this);
    if (auto* layout = qobject_cast<QVBoxLayout*>(this->layout())) {
        layout->addWidget(widget);
    }
}

qint64 PreviewTooltipDialog::positionForResume() const
{
    if (this->preview)
        return this->preview->resumePositionHint();
    return this->property("resumePosition").toLongLong();
}

bool PreviewTooltipDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == this->preview && this->mw) {
        switch (event->type()) {
        case QEvent::MouseButtonRelease: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::RightButton || mouse->button() == Qt::MiddleButton) {
                this->preview->jumpToRandomPosition();
                return true;
            }
            break;
        }
        case QEvent::Wheel: {
            auto* wheel = static_cast<QWheelEvent*>(event);
            int delta = wheel->angleDelta().y();
            if (delta == 0)
                delta = wheel->angleDelta().x();
            if (delta != 0) {
                double seconds = this->mw->App->config->get("preview_seek_seconds").toDouble();
                if (seconds > 0.0) {
                    double dir = delta > 0 ? 1.0 : -1.0;
                    this->preview->seekBySeconds(dir * seconds);
                    return true;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void PreviewTooltipDialog::closeEvent(QCloseEvent* e)
{
    if (this->preview) {
        this->preview->stopPreview(false);
    }
    qint64 resume = this->positionForResume();
    this->setProperty("resumePosition", resume);
    QDialog::closeEvent(e);
}
