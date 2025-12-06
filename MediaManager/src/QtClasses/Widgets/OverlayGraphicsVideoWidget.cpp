#include "stdafx.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QResizeEvent>
#include <QVideoSink>
#include <QVBoxLayout>

OverlayGraphicsVideoWidget::OverlayGraphicsVideoWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_NoSystemBackground, true);
    this->setAutoFillBackground(false);

    this->scene = new QGraphicsScene(this);
    this->view = new QGraphicsView(this->scene, this);
    this->view->setFrameShape(QFrame::NoFrame);
    this->view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->view->setAlignment(Qt::AlignCenter);
    this->view->setInteractive(false);
    this->view->setRenderHint(QPainter::Antialiasing, true);
    this->view->setRenderHint(QPainter::SmoothPixmapTransform, true);

    this->videoItem = new QGraphicsVideoItem();
    this->scene->addItem(this->videoItem);

    this->overlayBg = this->scene->addRect(QRectF(), QPen(Qt::NoPen), QBrush(QColor(0, 0, 0, 140)));
    this->overlayBg->setVisible(false);
    this->overlayText = this->scene->addSimpleText(QString());
    this->overlayText->setBrush(QBrush(Qt::white));
    this->overlayText->setVisible(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(this->view);
}

void OverlayGraphicsVideoWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    this->updateLayout();
}

void OverlayGraphicsVideoWidget::setOverlayText(const QString& text)
{
    if (this->currentText == text)
        return;
    this->currentText = text;
    if (!this->overlayText || !this->overlayBg)
        return;
    this->overlayText->setText(text);
    bool visible = !text.isEmpty();
    this->overlayText->setVisible(visible);
    this->overlayBg->setVisible(visible);
    this->updateLayout();
}

void OverlayGraphicsVideoWidget::updateLayout()
{
    if (!this->view || !this->scene || !this->videoItem)
        return;
    const QSizeF viewSize = this->view->viewport()->size();
    if (viewSize.isEmpty())
        return;

    this->videoItem->setSize(viewSize);
    this->scene->setSceneRect(QRectF(QPointF(0, 0), viewSize));

    if (!this->overlayText || !this->overlayBg || !this->overlayText->isVisible())
        return;

    QRectF textRect = this->overlayText->boundingRect();
    const qreal margin = 6.0;
    QRectF bgRect(0, 0, textRect.width() + 8.0, textRect.height() + 4.0);
    QPointF pos(viewSize.width() - bgRect.width() - margin, viewSize.height() - bgRect.height() - margin);
    bgRect.moveTopLeft(pos);
    this->overlayBg->setRect(bgRect);
    this->overlayText->setPos(bgRect.topLeft() + QPointF(4.0, 2.0));
}

QVideoSink* OverlayGraphicsVideoWidget::videoSink() const
{
    return this->videoItem ? this->videoItem->videoSink() : nullptr;
}
