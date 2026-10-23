#pragma once
#include <QDialog>
#include <QPointer>
#include <QPoint>
#include <QSet>
#include <QList>
#include "ui_InsertSettingsDialog.h"

class MainWindow;
class PreviewVideoWindow;
class QTreeWidgetItem;
class QEvent;
class QCloseEvent;

// Roles stored on each insert-tree item. The *Overridden flags let the bulk
// combo boxes refresh rows that were left on "Auto" without clobbering values
// the user typed in by hand.
namespace InsertRoles {
    enum roles {
        tagIds = Qt::UserRole + 10,
        authorOverridden = Qt::UserRole + 11,
        nameOverridden = Qt::UserRole + 12,
        typeOverridden = Qt::UserRole + 13,
        tagsOverridden = Qt::UserRole + 14
    };
}

class InsertSettingsDialog :
    public QDialog
{
    Q_OBJECT
public:
    InsertSettingsDialog(MainWindow* MW, QWidget* parent = nullptr);
    static QString get_author(QString path);
    static QString get_namedir(QString path);
    static QString get_name(QString path, QString dirsplit = "");
    static QString get_type(QString path);
    static QString get_maindir_path(QString path, QString dirsplit = "");
    void init_namedir(QStringList items);
    void init_namedir(QString path);
    void init_authors(QStringList items);
    void init_authors(QString path);
    void init_types(QStringList items);
    void init_types();
    void update_treewidget_author(QTreeWidgetItem* item);
    void update_treewidget_name(QTreeWidgetItem* item);
    void update_treewidget_type(QTreeWidgetItem* item);
    void update_treewidget();
    void update_treewidget(int col);
    QSet<int> itemTagIds(QTreeWidgetItem* item) const;
    void setItemTagIds(QTreeWidgetItem* item, const QSet<int>& ids);
    void editRowTags(QTreeWidgetItem* item);
    void editRowsTags(const QList<QTreeWidgetItem*>& items);
    void clearRowsTags(const QList<QTreeWidgetItem*>& items);
    void showRowPreview(QTreeWidgetItem* item);
    void resetRowOverrides(QTreeWidgetItem* item);
    void resetRowsOverrides(const QList<QTreeWidgetItem*>& items);
    ~InsertSettingsDialog();
    Ui::InsertSettingsDialog ui;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void done(int result) override;
private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onTreeContextMenu(const QPoint& point);
private:
    void setupTreeEditing();
    QString tagIdsDisplayText(const QSet<int>& ids) const;
    void setRowsTagIds(const QList<QTreeWidgetItem*>& items, const QSet<int>& ids);
    MainWindow* MW = nullptr;
    QPointer<PreviewVideoWindow> rowPreview;
    // Stays true until the first full update_treewidget() pass, so rows added
    // during population are not mistaken for manual edits.
    bool updatingTree = true;
};
