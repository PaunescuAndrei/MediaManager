#pragma once
#include <QDialog>
#include "ui_FilterDialog.h"
#include <QList>
#include <QJsonObject>

class MainWindow;

class FilterDialog :
    public QDialog
{
    Q_OBJECT
public:
    QCheckBox* all_authors = nullptr;
    QCheckBox* all_types = nullptr;
    FilterDialog(MainWindow* MW = nullptr,QJsonObject settings = QJsonObject(), QWidget* parent = nullptr);
    void authorsChanged(int state);
    void typesChanged(int state);
    QJsonObject toJson();
    ~FilterDialog();
    Ui::FilterDialog ui;
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
};

