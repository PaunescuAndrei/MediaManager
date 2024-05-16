#include "stdafx.h"
#include "shokoAPI.h"
#include "utils.h"
#include <QFileInfo>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QNetworkReply>
#include <QEventLoop>
#include "MainApp.h"

shokoAPI::shokoAPI(QString host_ip) {
    this->mgr = new QNetworkAccessManager();
    this->host_ip = host_ip;
}

bool shokoAPI::auth() {
    const QUrl url(this->host_ip + "/api/auth");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject obj;
    QString user, password;
    if (qMainApp) {
        user = qMainApp->config->get("shoko_user");
        password = qMainApp->config->get("shoko_password");
    }
    obj["user"] = user;
    obj["pass"] = password;
    obj["device"] = "MediaManager";
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson();
    // or
    // QByteArray data("{\"key1\":\"value1\",\"key2\":\"value2\"}");
    QNetworkReply* reply = this->mgr->post(request, data);
    QEventLoop loop;
    bool is_auth = false;
    QObject::connect(reply, &QNetworkReply::finished,&loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = doc.object();
            QJsonValue value = obj.value(QString("apikey"));
            if (!value.isUndefined()) {
                this->apikey = value.toString();
                is_auth = true;
            }
        }
        else {
            QString err = reply->errorString();
            qDebug() << err;
            qMainApp->logger->log(QString("Shoko API auth error:\n %1").arg(err), "ShokoAPI");
        }
        reply->deleteLater();
    });
    loop.exec();
    return is_auth;
}

bool shokoAPI::is_auth()
{
    if (!this->apikey.isEmpty())
        return true;
    return false;
}

QJsonArray shokoAPI::get_playlists() {
    QJsonArray output = QJsonArray();
    if (!this->is_auth())
        this->auth();
    if (!this->is_auth()) {
        qMainApp->logger->log(QString("Shoko API get_playlist error:\n Missing API key."), "ShokoAPI");
        return output;
    }

    const QUrl url(this->host_ip + "/v1/Playlist");
    QNetworkRequest request(url);
    request.setRawHeader("apikey", this->apikey.toUtf8());

    QNetworkReply* reply = this->mgr->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            output = doc.array();
        }
        else {
            QString err = reply->errorString();
            qDebug() << err;
            qMainApp->logger->log(QString("Shoko API get_playlist error:\n %1").arg(err), "ShokoAPI");
        }
        reply->deleteLater();
        });
    loop.exec();
    return output;
}

QJsonObject shokoAPI::get_playlist(QString PlaylistName, QJsonArray playlists) {
    for (const QJsonValue& playlist : playlists) {
        QJsonObject playlist_json = playlist.toObject();
        if (playlist_json.value("PlaylistName") == PlaylistName)
            return playlist_json;
    }
    return QJsonObject();
}

QJsonObject shokoAPI::get_playlist(QString PlaylistName) {
    QJsonArray playlists = this->get_playlists();
    QJsonObject playlist_output = this->get_playlist(PlaylistName,playlists);
    return playlist_output;
}

bool shokoAPI::update_playlist(QString PlaylistName, QJsonObject playlist_obj) {
    if (!this->is_auth())
        this->auth();
    if (!this->is_auth()) {
        qMainApp->logger->log(QString("Shoko API update_playlist error:\n Missing API key."), "ShokoAPI");
        return false;
    }
    QJsonObject playlist_to_update = this->get_playlist(PlaylistName);
    if (playlist_to_update.isEmpty()) {
        qMainApp->logger->log(QString("Shoko API update_playlist error:\n Cant find playlist \"%1\".").arg(PlaylistName), "ShokoAPI");
        return false;
    }
    playlist_obj.remove("PlaylistID");
    for (const QString& key : playlist_obj.keys()) {
        playlist_to_update[key] = playlist_obj[key];
    }

    const QUrl url(this->host_ip + "/v1/Playlist");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("apikey", this->apikey.toUtf8());

    QJsonDocument doc(playlist_to_update);
    QByteArray data = doc.toJson();

    QNetworkReply* reply = this->mgr->post(request, data);
    QEventLoop loop;
    bool success = true;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
        }
        else {
            success = false;
            QString err = reply->errorString();
            qDebug() << err;
            qMainApp->logger->log(QString("Shoko API update_playlist error:\n %1").arg(err), "ShokoAPI");
        }
        reply->deleteLater();
        });
    loop.exec();
    return success;
}

bool shokoAPI::update_playlist_items(QString PlaylistName, QString PlaylistItems)
{
    QJsonObject playlist_items = QJsonObject();
    playlist_items["PlaylistItems"] = PlaylistItems;
    return this->update_playlist(PlaylistName, playlist_items);
}

bool shokoAPI::check_if_exists(QString id) {
    if (!this->is_auth())
        this->auth();
    if (!this->is_auth()) {
        qMainApp->logger->log(QString("Shoko API check_if_exists error:\n Missing API key."), "ShokoAPI");
        return false;
    }

    const QUrl url(this->host_ip + "/api/v3/Series/" + id);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("apikey", this->apikey.toUtf8());

    QNetworkReply* reply = this->mgr->get(request);
    QEventLoop loop;
    bool success = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            success = true;
        }
        else {
            if (!(reply->error() == QNetworkReply::ContentNotFoundError && reply->readAll() == "No Series entry for the given seriesID")) {
                QString err = reply->errorString();
                qDebug() << err;
                qMainApp->logger->log(QString("Shoko API update_playlist error:\n %1").arg(err), "ShokoAPI");
            }
        }
        reply->deleteLater();
        });
    loop.exec();
    return success;
}

QPair<QString, QString> shokoAPI::get_first_item(QString items_input) {
    if (items_input.isEmpty())
        return QPair<QString, QString>();
    QStringList items = items_input.split("|");
    QString new_item = items.takeFirst();
    return { new_item,items.join("|") };
}

QPair<QString,QString> shokoAPI::get_random_item(QString items_input) {
    if (items_input.isEmpty())
        return QPair<QString, QString>();
    QStringList items = items_input.split("|");
    QString new_item = items.takeAt(utils::randint(0,items.size()-1));
    return {new_item,items.join("|")};
}

QString shokoAPI::combine_items(QString old_items_input,QString new_items_input) {
    QStringList old_items = old_items_input.split("|");
    QStringList new_items = new_items_input.split("|");
    old_items += new_items;
    old_items.removeDuplicates();
    return old_items.join("|");
}

int shokoAPI::clean_playlists() {
    int count = 0;
    QJsonArray playlists = this->get_playlists();
    for(const QJsonValue &playlist_value : playlists){
        QJsonObject playlist = playlist_value.toObject();
        QString playlistname = playlist.value("PlaylistName").toString();
        QString playlistitems = playlist.value("PlaylistItems").toString();
        if (!playlistitems.isEmpty()) {
            QStringList items_split = playlistitems.split("|");
            int len = items_split.size();
            for (QString& item : QStringList(items_split)) {
                QString item_id = item.section(';', 1);
                if (!this->check_if_exists(item_id)) {
                    count += items_split.removeAll(item);
                }
            }
            if(len != items_split.size())
                this->update_playlist_items(playlistname, items_split.join("|"));
        }
    }
    return count;
}

QJsonArray shokoAPI::get_import_folders() {
    QJsonArray output = QJsonArray();
    if (!this->is_auth())
        this->auth();
    if (!this->is_auth()) {
        qMainApp->logger->log(QString("Shoko API get_import_folders error:\n Missing API key."), "ShokoAPI");
        return output;
    }

    const QUrl url(this->host_ip + "/api/v3/ImportFolder");
    QNetworkRequest request(url);
    request.setRawHeader("apikey", this->apikey.toUtf8());

    QNetworkReply* reply = this->mgr->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            output = doc.array();
        }
        else {
            QString err = reply->errorString();
            qDebug() << err;
            qMainApp->logger->log(QString("Shoko API get_import_folders error:\n %1").arg(err), "ShokoAPI");
        }
        reply->deleteLater();
        });
    loop.exec();
    return output;
}

QStringList shokoAPI::get_files(QString id, QString type) {
    QStringList output_files = QStringList();
    if (!this->is_auth())
        this->auth();
    if (!this->is_auth()) {
        qMainApp->logger->log(QString("Shoko API getFiles error:\n Missing API key."), "ShokoAPI");
        return output_files;
    }

    if (type == "2") {
        //Series
        const QUrl url(this->host_ip + "/api/v3/Series/"+ id +"/File?pageSize=0&page=1&include=AbsolutePaths&includeDataFrom=Shoko");
        QNetworkRequest request(url);
        request.setRawHeader("apikey", this->apikey.toUtf8());

        QJsonArray files = QJsonArray();
        QNetworkReply* reply = this->mgr->get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
                QJsonValue _files = json.value("List");
                if (not _files.isUndefined()) {
                    files = _files.toArray();
                }
                else {
                    qDebug() << "Bad response, no List found.";
                    qMainApp->logger->log(QString("Shoko API getFiles error:\n %1").arg("Bad response, no List found."), "ShokoAPI");
                }
            }
            else {
                QString err = reply->errorString();
                qDebug() << err;
                qMainApp->logger->log(QString("Shoko API getFiles error:\n %1").arg(err), "ShokoAPI");
            }
            reply->deleteLater();
            });
        loop.exec();

        if (!files.isEmpty()) {
            for (const QJsonValue& file_rel_json : files) {
                QJsonObject file_location = file_rel_json.toObject().value("Locations").toArray().first().toObject();
                QString full_path = file_location.value("AbsolutePath").toString();
                output_files.append(full_path);
            }
        }
    }
    else if (type == "1") {
        //Episode
        const QUrl url(this->host_ip + "/api/v3/Episode/" + id + "?includeFiles=true&includeMediaInfo=false&includeAbsolutePaths=false&includeDataFrom=Shoko");
        QNetworkRequest request(url);
        request.setRawHeader("apikey", this->apikey.toUtf8());

        QJsonArray files = QJsonArray();
        QNetworkReply* reply = this->mgr->get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
                QJsonValue _files = json.value("Files");
                if (not _files.isUndefined()) {
                    files = _files.toArray();
                }
                else {
                    qDebug() << "Bad response, no Files found.";
                    qMainApp->logger->log(QString("Shoko API getFiles error:\n %1").arg("Bad response, no Files found."), "ShokoAPI");
                }
            }
            else {
                QString err = reply->errorString();
                qDebug() << err;
                qMainApp->logger->log(QString("Shoko API getFiles error:\n %1").arg(err), "ShokoAPI");
            }
            reply->deleteLater();
            });
        loop.exec();

        if (!files.isEmpty()) {
            QJsonArray import_folders_json_arr = this->get_import_folders();
            QMap<QString, QString> import_folders = QMap<QString, QString>();
            for (const QJsonValue& item_json : import_folders_json_arr) {
                QJsonObject import_folder = item_json.toObject();
                import_folders[import_folder.value("ID").toString()] = import_folder.value("Path").toString();
            }
            for (const QJsonValue& file_rel_json : files) {
                QJsonObject file_location = file_rel_json.toObject().value("Locations").toArray().first().toObject();
                QString full_path = QDir::toNativeSeparators(import_folders.value(file_location.value("ImportFolderID").toString()) + file_location.value("RelativePath").toString());
                output_files.append(full_path);
            }
        }
    }
    return output_files;
}

shokoAPI::~shokoAPI()
{
    delete this->mgr;
}
