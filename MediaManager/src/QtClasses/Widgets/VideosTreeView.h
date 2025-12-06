#pragma once
#include <QTreeView>
#include <QMouseEvent>

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
    void mousePressEvent(QMouseEvent* event) override;
};
