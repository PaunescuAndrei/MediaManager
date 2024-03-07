#pragma once
#include <QDialog>
#include "ui_addWatchedDialog.h"
class addWatchedDialog : public QDialog
{
    Q_OBJECT
public:
    addWatchedDialog(QWidget* parent = nullptr);
    ~addWatchedDialog();
    Ui::addWatchedDialog ui;
};