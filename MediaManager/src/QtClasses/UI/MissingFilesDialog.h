#pragma once
#include <QDialog>
#include "ui_MissingFilesDialog.h"
#include "VideoData.h"
#include <QVector>

class MissingFilesDialog : public QDialog
{
    Q_OBJECT

public:
    MissingFilesDialog(const QVector<VideoData>& allVideos, QWidget* parent = nullptr);
    ~MissingFilesDialog();

    bool hasChanged() const { return m_changed; }

private slots:
    void onDeleteSelected();
    void onDeleteAll();

private:
    void scanFiles(const QVector<VideoData>& allVideos);
    void updateStatusLabel();

    Ui::MissingFilesDialog ui;
    QVector<VideoData> m_missingVideos;
    bool m_changed = false;
};
