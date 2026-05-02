#include "stdafx.h"
#include "MissingFilesDialog.h"
#include "MainApp.h"
#include <QFileInfo>
#include <QMessageBox>

MissingFilesDialog::MissingFilesDialog(const QVector<VideoData>& allVideos, QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);

    connect(ui.deleteSelectedBtn, &QPushButton::clicked, this, &MissingFilesDialog::onDeleteSelected);
    connect(ui.deleteAllBtn, &QPushButton::clicked, this, &MissingFilesDialog::onDeleteAll);
    connect(ui.closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    scanFiles(allVideos);
}

MissingFilesDialog::~MissingFilesDialog()
{
}

void MissingFilesDialog::scanFiles(const QVector<VideoData>& allVideos)
{
    m_missingVideos.clear();
    ui.missingFilesList->clear();

    for (const auto& video : allVideos) {
        if (!QFileInfo::exists(video.path)) {
            m_missingVideos.push_back(video);
            ui.missingFilesList->addItem(video.path);
        }
    }

    updateStatusLabel();
}

void MissingFilesDialog::updateStatusLabel()
{
    ui.statusLabel->setText(QString("Found %1 missing files in the database.").arg(m_missingVideos.size()));
    ui.deleteSelectedBtn->setEnabled(!m_missingVideos.isEmpty());
    ui.deleteAllBtn->setEnabled(!m_missingVideos.isEmpty());
}

void MissingFilesDialog::onDeleteSelected()
{
    auto selectedItems = ui.missingFilesList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Deletion",
        QString("Are you sure you want to delete %1 selected entries from the database?").arg(selectedItems.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Iterate backwards to avoid index issues when removing
        for (int i = selectedItems.size() - 1; i >= 0; --i) {
            auto* item = selectedItems[i];
            int row = ui.missingFilesList->row(item);
            int videoId = m_missingVideos[row].id;

            qMainApp->db->deleteVideo(videoId);
            
            // Remove from our local tracking
            m_missingVideos.removeAt(row);
            delete ui.missingFilesList->takeItem(row);
        }
        m_changed = true;
        updateStatusLabel();
    }
}

void MissingFilesDialog::onDeleteAll()
{
    if (m_missingVideos.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Deletion",
        QString("Are you sure you want to delete ALL %1 missing entries from the database?").arg(m_missingVideos.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        for (const auto& video : m_missingVideos) {
            qMainApp->db->deleteVideo(video.id);
        }
        m_missingVideos.clear();
        ui.missingFilesList->clear();
        m_changed = true;
        updateStatusLabel();
    }
}
