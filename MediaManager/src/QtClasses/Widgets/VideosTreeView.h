#pragma once
#include <QTreeView>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionViewItem>

class VideosTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit VideosTreeView(QWidget* parent = nullptr);
    ~VideosTreeView() override = default;

    void findAndScrollToDelayed(QString value, bool select = false, QAbstractItemView::ScrollHint scrollhint = QAbstractItemView::EnsureVisible);
    void scrollToVerticalPositionDelayed(int scroll_value);
    void scrollToDelayed(const QModelIndex& index, QAbstractItemView::ScrollHint scrollhint = QAbstractItemView::EnsureVisible);
    void configureHeader();
    // Set while the background layer is on: Qt cannot express a list that is translucent *and*
    // alternating -- Base rows take their colour from the viewport's autofill, off over the layer, so
    // one row class is left with nothing behind it. Off is Qt's own path, byte-for-byte.
    void setTranslucentRows(bool translucent);

signals:
    void itemMiddleClicked(const QModelIndex& index);
    void itemAltClicked(const QModelIndex& index);

protected:
    // Filled unconditionally, the alpha-255 case included: at 255 the rows must be opaque and the
    // viewport's autofill is off over the layer, so skipping the fill would leave them unpainted.
    void drawRow(QPainter* painter, const QStyleOptionViewItem& options,
        const QModelIndex& index) const override;
    // Qt leaves the space under a short list unpainted; over the layer it reads as a hole, so it is
    // filled with the Base row colour at the list's alpha -- the surface the strip above the list gets.
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // An Alt+click is the preview gesture, not a selection gesture. The press is
    // intercepted below, so the matching release/move/double-click must not fall
    // through to QTreeView's selection logic either.
    bool altPreviewPressed = false;
    bool translucentRows = false;
};
