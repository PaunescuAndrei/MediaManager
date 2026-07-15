#pragma once
#include <QDialog>
#include "ui_finishDialog.h"
#include <QTimer>
#include <QSharedPointer>

class MainWindow;
class BasePlayer;

class finishDialog : public QDialog
{
   Q_OBJECT
public:
   enum class PlayerContext { MainPlayer, WatchSelected, WatchExternal };

   finishDialog(MainWindow* MW, QSharedPointer<BasePlayer> player, PlayerContext context, QWidget* parent = nullptr);
   bool eventFilter(QObject* obj, QEvent* event);
   ~finishDialog();
   void wheelEvent(QWheelEvent* event) override;
   void updateCountdownText();
   void stopCountdown();
   void updateWindowTitle();
   void configureForContext();
   MainWindow* MW = nullptr;
   QSharedPointer<BasePlayer> m_player;
   PlayerContext m_context = PlayerContext::MainPlayer;
   QTimer timer;
   QTimer countdownTimer;
   QTimer titleUpdateTimer;
   int countdownSeconds = 10;
   Ui::finishDialog ui;
   enum CustomDialogCode {
       Skip = 100,
       Replay = 101
   };
private:
};