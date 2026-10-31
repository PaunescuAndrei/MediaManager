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

signals:
    void itemMiddleClicked(const QModelIndex& index);
    void itemAltClicked(const QModelIndex& index);

protected:
    // (M5 Task 8b: the row fill, once per row. Qt fills the AlternateBase rows itself and leaves the
    // Base rows to the viewport's autofill, so a translucent list can only be one of two things without
    // this: the viewport filling every row, which makes alternate rows composite twice and run at
    // alpha*alpha, or alternate rows filled and Base rows not filled at all. Filling here means every
    // row is one fill at whatever alpha the palette carries, and the alternation is the theme's own two
    // colours. An opaque palette is left entirely to Qt.)
    void drawRow(QPainter* painter, const QStyleOptionViewItem& options,
        const QModelIndex& index) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // An Alt+click is the preview gesture, not a selection gesture. The press is
    // intercepted below, so the matching release/move/double-click must not fall
    // through to QTreeView's selection logic either.
    bool altPreviewPressed = false;
};
