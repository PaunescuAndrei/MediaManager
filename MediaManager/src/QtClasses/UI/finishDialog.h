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
   bool eventFilter(QObject* obj, QEvent* event);  
   ~finishDialog();  
   void wheelEvent(QWheelEvent* event) override;  
   void updateCountdownText();  
   void stopCountdown();  
   void updateWindowTitle();
   MainWindow* MW = nullptr;
   QTimer timer;  
   QTimer countdownTimer;  
   QTimer titleUpdateTimer;
   int countdownSeconds = 10;  
   Ui::finishDialog ui;  
   enum CustomDialogCode {  
       Skip = 100,  
       Replay = 101  
   };  
};

