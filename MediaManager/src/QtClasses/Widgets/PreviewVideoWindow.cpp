#include "stdafx.h"
#include "PreviewVideoWindow.h"

#include <QQuickItem>
#include <QVideoSink>
#include <QQmlEngine>
#include <QQmlError>
#include <QUrl>
#include <QColor>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>
#include "PreviewPlaybackController.h"
#include "OverlayGraphicsVideoWidget.h"
#include "utils.h"

PreviewVideoWindow::PreviewVideoWindow(QWidget* transientParent)
    : QQuickView(OverlayGraphicsVideoWidget::previewEngine(), nullptr)
{
    // A real QQuickWindow popup: owned by (transient to) the main window/dialog, no
    // QQuickWidget and no QWidget child hierarchy, so the parent window is not dragged
    // into a composition change when the preview appears.
    this->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    this->setColor(QColor(17, 17, 17));
    this->setResizeMode(QQuickView::SizeRootObjectToView);
    QQuickView::setSource(QUrl(QStringLiteral("qrc:/qml/PreviewVideo.qml")));

    if (this->status() == QQuickView::Error) {
        const QList<QQmlError> errors = this->errors();
        for (const QQmlError& error : errors)
            qWarning() << "PreviewVideo.qml (native popup):" << error.toString();
    }

    this->root = this->rootObject();
    QVideoSink* sink = this->root ? this->root->findChild<QVideoSink*>() : nullptr;

    this->controller = new PreviewPlaybackController(this);
    this->controller->setVideoSink(sink);
    QObject::connect(this->controller, &PreviewPlaybackController::overlayTextChanged, this, &PreviewVideoWindow::applyOverlayText);
    QObject::connect(this->controller, &PreviewPlaybackController::videoSizeKnown, this, &PreviewVideoWindow::videoSizeKnown);
    QObject::connect(this->controller, &PreviewPlaybackController::previewStarted, this, &PreviewVideoWindow::previewStarted);
    QObject::connect(this->controller, &PreviewPlaybackController::previewStopped, this, &PreviewVideoWindow::previewStopped);
    QObject::connect(this->controller, &PreviewPlaybackController::firstFrameReady, this, &PreviewVideoWindow::firstFrameReady);
    QObject::connect(this->controller, &PreviewPlaybackController::firstFrameReady, this, &PreviewVideoWindow::uncloak);

    this->applyOverlayStyle();
    this->applyOverlayText(QString());

    if (transientParent) {
        QWidget* topLevel = transientParent->window();
        if (topLevel) {
            if (!topLevel->windowHandle())
                topLevel->winId();
            if (topLevel->windowHandle())
                this->setTransientParent(topLevel->windowHandle());
        }
        // Owned windows are closed when the owner goes away; Qt also destroys the
        // transient window with its parent, but close first so finished() is emitted.
        QObject::connect(transientParent, &QObject::destroyed, this, &QWindow::close);
    }

    // DWM-cloak until the first frame (or a short safety timeout) so the Windows origin
    // ghost is never presented. This is the same compositor workaround used elsewhere.
    utils::set_window_cloaked(this, true);
    QTimer::singleShot(500, this, [this] { this->uncloak(); });

    QObject::connect(this, &QQuickWindow::closing, this, &PreviewVideoWindow::handleClose);
    if (qApp)
        qApp->installEventFilter(this);
}

PreviewVideoWindow::~PreviewVideoWindow()
{
    if (qApp)
        qApp->removeEventFilter(this);
    // Detach the worker-thread player while the QML scene and its QVideoSink are still
    // alive; the QQuickView base destructor releases the scene afterwards.
    if (this->controller)
        this->controller->shutdown();
}

void PreviewVideoWindow::setSeekSeconds(double seconds)
{
    this->seekSeconds = std::max(0.0, seconds);
}

void PreviewVideoWindow::setSource(const QString& path)
{
    this->controller->setSource(path);
}

void PreviewVideoWindow::setStartDelay(int ms)
{
    this->controller->setStartDelay(ms);
}

void PreviewVideoWindow::setVolume(int percent)
{
    this->controller->setVolume(percent);
}

void PreviewVideoWindow::setRandomStartEnabled(bool enabled)
{
    this->controller->setRandomStartEnabled(enabled);
}

void PreviewVideoWindow::setMuted(bool muted)
{
    this->controller->setMuted(muted);
}

void PreviewVideoWindow::setRememberPositionEnabled(bool enabled)
{
    this->controller->setRememberPositionEnabled(enabled);
}

void PreviewVideoWindow::setRandomOnHoverEnabled(bool enabled)
{
    this->controller->setRandomOnHoverEnabled(enabled);
}

void PreviewVideoWindow::setSeededRandom(bool enabled, const QString& seedValue)
{
    this->controller->setSeededRandom(enabled, seedValue);
}

void PreviewVideoWindow::setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin)
{
    this->overlayScaleMultiplier = std::max(0.1, scaleMultiplier);
    this->overlayPadX = std::max(0, padX);
    this->overlayPadY = std::max(0, padY);
    this->overlayMargin = std::max(0, margin);
    this->applyOverlayStyle();
}

void PreviewVideoWindow::jumpToRandomPosition()
{
    this->controller->jumpToRandomPosition();
}

void PreviewVideoWindow::seekBySeconds(double seconds)
{
    this->controller->seekBySeconds(seconds);
}

void PreviewVideoWindow::prepareInitialFrame(bool keepPlaying)
{
    this->controller->prepareInitialFrame(keepPlaying);
}

void PreviewVideoWindow::restoreLastPosition(qint64 positionMs)
{
    this->controller->restoreLastPosition(positionMs);
}

qint64 PreviewVideoWindow::positionForResume() const
{
    return this->controller ? this->controller->resumePositionHint() : 0;
}

bool PreviewVideoWindow::isPlaying() const
{
    return this->controller && this->controller->isPlaying();
}

void PreviewVideoWindow::startPreview()
{
    this->controller->startPreview();
}

void PreviewVideoWindow::stopPreview(bool forceStop)
{
    this->controller->stopPreview(forceStop);
}

bool PreviewVideoWindow::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::RightButton || mouse->button() == Qt::MiddleButton) {
            this->jumpToRandomPosition();
            return true;
        }
        break;
    }
    case QEvent::Wheel: {
        auto* wheel = static_cast<QWheelEvent*>(event);
        int delta = wheel->angleDelta().y();
        if (delta == 0)
            delta = wheel->angleDelta().x();
        if (delta != 0 && this->seekSeconds > 0.0) {
            const double direction = delta > 0 ? 1.0 : -1.0;
            this->seekBySeconds(direction * this->seekSeconds);
            return true;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            this->close();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QQuickView::event(event);
}

bool PreviewVideoWindow::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (!this->isVisible())
        return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!this->geometry().contains(mouse->globalPosition().toPoint())) {
            // Mirror the old Qt::Popup behavior for a plain outside click, but let an
            // Alt+click through: that is the "preview this row" gesture, so the click
            // must reach the list and open the newly selected preview instead of being
            // swallowed while only the old preview closes.
            const bool previewGesture = mouse->modifiers().testFlag(Qt::AltModifier);
            this->close();
            return !previewGesture;
        }
        break;
    }
    case QEvent::KeyPress: {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            this->close();
            return true;
        }
        break;
    }
    case QEvent::ApplicationDeactivate:
        this->close();
        break;
    default:
        break;
    }
    return false;
}

void PreviewVideoWindow::handleClose()
{
    if (this->closing)
        return;
    this->closing = true;
    if (this->controller)
        this->controller->stopPreview(false);
    emit this->finished();
    this->deleteLater();
}

void PreviewVideoWindow::uncloak()
{
    if (this->uncloaked)
        return;
    this->uncloaked = true;
    utils::set_window_cloaked(this, false);
}

void PreviewVideoWindow::applyOverlayStyle()
{
    if (!this->root)
        return;
    this->root->setProperty("overlayScaleMultiplier", this->overlayScaleMultiplier);
    this->root->setProperty("overlayPadX", this->overlayPadX);
    this->root->setProperty("overlayPadY", this->overlayPadY);
    this->root->setProperty("overlayMargin", this->overlayMargin);
}

void PreviewVideoWindow::applyOverlayText(const QString& text)
{
    if (!this->root)
        return;
    this->root->setProperty("overlayText", text);
}
