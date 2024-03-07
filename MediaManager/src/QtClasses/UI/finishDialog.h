#pragma once
#include <QDialog>
#include "ui_finishDialog.h"
#include <QTimer>

class MainWindow;

class finishDialog : public QDialog
{
    Q_OBJECT
public:
    finishDialog(MainWindow* MW = nullptr, QWidget* parent = nullptr);
    ~finishDialog();
    void wheelEvent(QWheelEvent* event) override;
    QTimer timer;
    Ui::finishDialog ui;
    enum CustomDialogCode {Skip=100};
};

