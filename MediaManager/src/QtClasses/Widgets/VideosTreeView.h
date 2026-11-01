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
    // Whether this view paints its own row backgrounds. It is set while the background
    // layer is on, because Qt's own arrangement cannot express a list that is translucent *and*
    // alternating: Base rows take their colour from the viewport's autofill, which is switched off over
    // the layer, so without this one of the two row classes is left with nothing behind it. Off is
    // Qt's own path, byte for byte, which is what keeps the layer-off rendering identical.
    void setTranslucentRows(bool translucent);

signals:
    void itemMiddleClicked(const QModelIndex& index);
    void itemAltClicked(const QModelIndex& index);

protected:
    // The row fill, once per row, unconditionally -- the alpha-255 case included. Qt fills
    // the AlternateBase rows itself and leaves the Base rows to the viewport's autofill, so a list that
    // is not running Qt's own opaque path has to be filled here or one of the two row classes is left
    // with nothing behind it. One fill at whatever alpha the palette carries, and the alternation is
    // the theme's own two colours.
    void drawRow(QPainter* painter, const QStyleOptionViewItem& options,
        const QModelIndex& index) const override;
    // The space under a short list, painted with the Base row colour at the list's alpha --
    // Qt leaves it unpainted, and over the layer that reads as a hole in the list.
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
    // See setTranslucentRows().
    bool translucentRows = false;
};
