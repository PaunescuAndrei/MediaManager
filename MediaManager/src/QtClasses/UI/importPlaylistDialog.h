#pragma once
#include <QDialog>
#include "ui_importPlaylistDialog.h"

class importPlaylistDialog :
    public QDialog
{
    Q_OBJECT
public:
    importPlaylistDialog(QWidget* parent = nullptr);
    ~importPlaylistDialog();
    Ui::importPlaylistDialog ui;
};

