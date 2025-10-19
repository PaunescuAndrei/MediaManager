#pragma once
#include <QDialog>
#include "ui_LogDialog.h"
#include <QTimer>
#include <QList>
#include "logger.h"

class LogDialog :
    public QDialog
{
    Q_OBJECT
public:
    LogDialog(QWidget* parent = nullptr);
    void insertNewItem(QString Date, QString Message, QString Type, QString Extra_Data = QString());
    ~LogDialog();
    Ui::LogDialog ui;
private slots:
    void processBufferedLogs();
private:
    QList<log_message> logBuffer;
    QTimer* logTimer;
};


