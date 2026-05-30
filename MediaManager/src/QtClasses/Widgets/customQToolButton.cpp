#include "stdafx.h"
#include "customQToolButton.h"
#include <QEvent>
#include <Qt>

customQToolButton::customQToolButton(QWidget* parent) : QToolButton(parent)
{
}

customQToolButton::customQToolButton(const QString& text, QWidget* parent) : QToolButton(parent)
{
    setText(text);
}

void customQToolButton::mousePressEvent(QMouseEvent* e)
{
    QToolButton::mousePressEvent(e);
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

void customQToolButton::mouseMoveEvent(QMouseEvent* e)
{
    QToolButton::mouseMoveEvent(e);
    if (this->m_pressedButton != Qt::NoButton && (e->buttons() & this->m_pressedButton)) {
        this->setDown(this->hitButton(e->position().toPoint()));
        e->accept();
    }
}

void customQToolButton::mouseReleaseEvent(QMouseEvent* e)
{
    QToolButton::mouseReleaseEvent(e);
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

customQToolButton::~customQToolButton()
{
}
