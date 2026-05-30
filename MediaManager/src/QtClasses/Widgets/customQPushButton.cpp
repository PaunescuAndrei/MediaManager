#include "stdafx.h"
#include "customQPushButton.h"
#include <QEvent>
#include <Qt>

customQPushButton::customQPushButton(QWidget* parent) : QPushButton(parent)
{
}

customQPushButton::customQPushButton(const QString& text, QWidget* parent) : QPushButton(text, parent)
{
}

void customQPushButton::mousePressEvent(QMouseEvent* e)
{
    QPushButton::mousePressEvent(e);
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        if (this->m_pressedButton == Qt::NoButton && this->hitButton(e->position().toPoint())) {
            this->m_pressedButton = e->button();
            if (!this->isDown()) {
                this->setDown(true);
            }
            this->repaint();
            e->accept();
            return;
        }
        e->ignore();
        return;
    }
}

void customQPushButton::mouseMoveEvent(QMouseEvent* e)
{
    QPushButton::mouseMoveEvent(e);
    if (this->m_pressedButton != Qt::NoButton && (e->buttons() & this->m_pressedButton)) {
        this->setDown(this->hitButton(e->position().toPoint()));
        e->accept();
    }
}

void customQPushButton::mouseReleaseEvent(QMouseEvent* e)
{
    QPushButton::mouseReleaseEvent(e);
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        const bool isOwned = (e->button() == this->m_pressedButton);
        if (isOwned) {
            this->m_pressedButton = Qt::NoButton;
            if (this->hitButton(e->position().toPoint())) {
                if (e->button() == Qt::RightButton) {
                    emit this->rightClicked();
                }
                else {
                    emit this->middleClicked();
                }
            }
        }
        if (!e->buttons()) {
            this->setDown(false);
        }
        e->accept();
        return;
    }
}

customQPushButton::~customQPushButton()
{
}
