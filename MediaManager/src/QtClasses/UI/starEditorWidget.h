#pragma once
#include <QWidget>
#include "starrating.h"
#include <QPersistentModelIndex>

class starEditorWidget : public QWidget
{
    Q_OBJECT
public:
    enum EditMode {NoEdit,DoubleClick,HoverClick};
    QPersistentModelIndex item_index;
    bool editing_finished = true;
    EditMode edit_mode = EditMode::NoEdit;
    double original_value = 0;
    starEditorWidget(QWidget *parent = nullptr, QPersistentModelIndex item_index = QPersistentModelIndex());
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void setStarRating(const StarRating &starRating) {
        const int previousPixelSize = myStarRating.starPixelSize();
        myStarRating = starRating;
        if (previousPixelSize > 0) {
            myStarRating.setStarPixelSize(previousPixelSize);
        }
        original_value = starRating.starCount();
    }
    void setEditMode(EditMode editMode) {
        this->edit_mode = editMode;
    }
    void setStarPixelSize(int pixelSize);
    int starPixelSize() const;
    StarRating starRating() { return myStarRating; }
signals:
    void editingFinished();
protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    double starAtPosition(double x) const;

    StarRating myStarRating;
};
