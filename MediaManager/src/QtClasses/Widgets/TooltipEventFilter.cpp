#include "stdafx.h"
#include "TooltipEventFilter.h"
#include "MainApp.h"
#include <QWidget>
#include <QEvent>
#include <QToolTip>
#include <QCursor>
#include <QHelpEvent>
#include <QAbstractItemView>

TooltipEventFilter::TooltipEventFilter(MainApp* app)
    : QObject(app)
    , mApp(app)
{
    this->showTimer = new QTimer(this);
    this->showTimer->setSingleShot(true);
    connect(this->showTimer, &QTimer::timeout, this, [this] {
        if (this->pendingTarget && this->enabled) {
            QToolTip::showText(this->pendingPos, this->pendingTarget->toolTip(), this->pendingTarget);
        }
    });
    this->reloadConfig();
}

void TooltipEventFilter::reloadConfig() {
    this->enabled = this->mApp->config->get_bool("tooltips_enabled");
    this->delayMs = qBound(100, this->mApp->config->get("tooltip_delay_ms").toInt(), 5000);
}

bool TooltipEventFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        // Globally disabled — block all tooltips except item-view items (they
        // serve a different purpose: showing truncated cell content). We may
        // add a separate setting for item-view tooltips in the future.
        if (!this->enabled) {
            QWidget* w = qobject_cast<QWidget*>(obj);
            if (w && qobject_cast<QAbstractItemView*>(w->parentWidget()))
                return QObject::eventFilter(obj, event);
            return true;
        }
        // Block native tooltip only for widgets we've claimed via Enter;
        // everything else (viewports, comboboxes, menus, etc.) passes through
        if (qobject_cast<QWidget*>(obj) == this->pendingTarget)
            return true;
        return QObject::eventFilter(obj, event);
    }

    if (!this->enabled)
        return QObject::eventFilter(obj, event);

    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget || widget->toolTip().isEmpty())
        return QObject::eventFilter(obj, event);

    switch (event->type()) {
        case QEvent::Enter:
            this->stopShowTimer();
            this->pendingTarget = widget;
            this->pendingPos = QCursor::pos();
            this->startShowTimer();
            break;

        case QEvent::MouseMove:
            if (this->pendingTarget == widget) {
                this->pendingPos = QCursor::pos();
            }
            break;

        case QEvent::Leave:
            if (this->pendingTarget == widget) {
                this->stopShowTimer();
                this->pendingTarget = nullptr;
                QToolTip::hideText();
            }
            break;

        default:
            break;
    }

    return QObject::eventFilter(obj, event);
}

void TooltipEventFilter::startShowTimer() {
    this->showTimer->start(this->delayMs);
}

void TooltipEventFilter::stopShowTimer() {
    this->showTimer->stop();
}
