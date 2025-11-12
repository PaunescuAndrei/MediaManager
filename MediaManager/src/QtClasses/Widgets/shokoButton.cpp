#include "stdafx.h"
#include "shokoButton.h"
#include <QEvent>
#include <Qt>
#include "utils.h"
#include <QProcess>
#include "MainApp.h"
#include "MainWindow.h"
#include <QFileInfo>
#include <QToolTip>
#include <QMessageBox>
#include "shokoAPI.h"
#include <importPlaylistDialog.h>

shokoButton::shokoButton(QWidget* parent) : QToolButton(parent) {
    connect(this, &QToolButton::clicked, this, &shokoButton::button_clicked);
}

void shokoButton::setMainWindow(MainWindow* window) {
    this->MW = window;
}

void shokoButton::update_new_watching(QString ptw_playlist, bool random) {
    QJsonObject watching = qMainApp->shoko_API->get_playlist("1. Watching");
    QJsonObject ptw = qMainApp->shoko_API->get_playlist(ptw_playlist);
    if (watching.isEmpty() or ptw.isEmpty()) {
        QMessageBox msg = QMessageBox(this->MW);
        msg.setWindowTitle("ShokoAPI");
        msg.setText("Problems establishing connections.");
        msg.exec();
        return;
    }
    QPair<QString, QString> new_ptw_items;
    if(random)
        new_ptw_items = qMainApp->shoko_API->get_random_item(ptw["PlaylistItems"].toString());
    else
        new_ptw_items = qMainApp->shoko_API->get_first_item(ptw["PlaylistItems"].toString());

    if (new_ptw_items.first.isEmpty()) {
        QMessageBox msg = QMessageBox(this->MW);
        msg.setWindowTitle("ShokoAPI");
        msg.setText(QStringLiteral("\"%1\" Playlist is empty.").arg(ptw_playlist));
        msg.exec();
        return;
    }
    if (!new_ptw_items.first.isEmpty()) {
        QString new_watching_items = qMainApp->shoko_API->combine_items(watching["PlaylistItems"].toString(), new_ptw_items.first);
        QStringList item_type_id = new_ptw_items.first.split(';');
        if (item_type_id.length() != 2) {
            qDebug() << "Bad Playlist item format.";
            qMainApp->logger->log(QStringLiteral("Shoko update_new_watching error:\n Bad Playlist item format, %1").arg(new_ptw_items.first), "ShokoButton");
            return;
        }
        QStringList files = qMainApp->shoko_API->get_files(item_type_id[1],item_type_id[0]);
        bool inserted = this->MW->InsertVideoFiles(files, true, "PLUS", "HT");
        if (inserted) {
            qMainApp->shoko_API->update_playlist_items("1. Watching", new_watching_items);
            qMainApp->shoko_API->update_playlist_items(ptw_playlist, new_ptw_items.second);
        }
    }
}

void shokoButton::enterEvent(QEnterEvent* event)
{
    this->setIcon(QIcon(":/shoko/resources/shoko_icon_hover.png"));
    QToolButton::enterEvent(event);
}

void shokoButton::leaveEvent(QEvent* event)
{
    this->setIcon(QIcon(":/shoko/resources/shoko_icon.png"));
    QToolButton::leaveEvent(event);
}

void shokoButton::mousePressEvent(QMouseEvent* e)
{
    this->setIcon(QIcon(":/shoko/resources/shoko_icon_pressed.png"));
    QToolButton::mousePressEvent(e);
}

void shokoButton::mouseReleaseEvent(QMouseEvent* e)
{
    this->setIcon(QIcon(":/shoko/resources/shoko_icon_hover.png"));
    if (e->button() == Qt::RightButton) {
        importPlaylistDialog qDialog = importPlaylistDialog(this->MW);
        QJsonArray playlists_json = qMainApp->shoko_API->get_playlists();
        for (auto item : playlists_json) {
            qDialog.ui.comboBox_playlist->addItem(item.toObject().value("PlaylistName").toString());
        }
        qDialog.ui.comboBox_playlist->setCurrentIndex(qDialog.ui.comboBox_playlist->findText(qMainApp->config->get("shoko_import_playlist")));
        qDialog.ui.checkBox_random->setChecked(qMainApp->config->get_bool("shoko_import_random_item"));
        if (qDialog.exec() == QDialog::Accepted) {
            qMainApp->config->set("shoko_import_playlist", qDialog.ui.comboBox_playlist->currentText());
            qMainApp->config->set("shoko_import_random_item", utils::bool_to_text_qt(qDialog.ui.checkBox_random->isChecked()));
            qMainApp->config->save_config();
            this->update_new_watching(qDialog.ui.comboBox_playlist->currentText(),qDialog.ui.checkBox_random->isChecked());
        }
    }
    else if (e->button() == Qt::MiddleButton) {
        int i = qMainApp->shoko_API->clean_playlists();
        QMessageBox msg = QMessageBox(this->MW);
        msg.setWindowTitle("Shoko DB Clean Playlist");
        msg.setText(QStringLiteral("Cleaned %1 entries.").arg(i));
        msg.exec();
    }
    QToolButton::mouseReleaseEvent(e);
}

void shokoButton::button_clicked() {
    DWORD id = utils::checkProcRunning(QFileInfo(this->MW->App->config->get("shoko_path")).fileName().toStdString());
    if (id > 0) {
        utils::bring_pid_to_foreground(id);
    }
    else {
        QProcess p = QProcess();
        p.setProgram(this->MW->App->config->get("shoko_path"));
        p.startDetached();
    }
}

shokoButton::~shokoButton() {

}