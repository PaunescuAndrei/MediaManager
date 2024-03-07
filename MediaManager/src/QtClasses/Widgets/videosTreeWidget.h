#pragma once
#include <QTreeWidget>
#include <QEvent>
#include <QStringList>
#include <QAbstractItemView>

class videosTreeWidget :
    public QTreeWidget
{
    Q_OBJECT
public:
    QTreeWidgetItem* last_selected = nullptr;
    videosTreeWidget(QWidget* parent = nullptr);
    ~videosTreeWidget();
    void mousePressEvent(QMouseEvent* event) override;
    void clear();
    QList<QTreeWidgetItem*> findItemsCustom(const QString& text, Qt::MatchFlags flags, int column = 0, int hits = -1);
    void scrollToItemCustom(QTreeWidgetItem* item, bool select, QAbstractItemView::ScrollHint scrollhint = QAbstractItemView::EnsureVisible);
    void scrollToItemDelayed(QTreeWidgetItem* item, bool select = false, QAbstractItemView::ScrollHint scrollhint = QAbstractItemView::EnsureVisible);
    void findAndScrollToDelayed(QString value, bool select = false, QAbstractItemView::ScrollHint scrollhint = QAbstractItemView::EnsureVisible);
    void scrollToVerticalPositionDelayed(int scroll_value);
signals:
    void itemMiddleClicked(QTreeWidgetItem* item, int column);
};
