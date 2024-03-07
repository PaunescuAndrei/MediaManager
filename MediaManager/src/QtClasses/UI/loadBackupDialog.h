#pragma once
#include <QDialog>
#include "ui_loadBackupDialog.h"

class loadBackupDialog :
    public QDialog
{
    Q_OBJECT
public:
    loadBackupDialog(QWidget* parent = nullptr);
    ~loadBackupDialog();
    Ui::loadBackupDialog ui;
};

