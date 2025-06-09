#include "stdafx.h"
#include "UpdatePathsDialog.h"
#include "InsertSettingsDialog.h"
#include <QFileInfo>
#include "MainWindow.h"
#include "utils.h"
#include <rapidfuzz_all.hpp>
#include <QClipBoard>

UpdatePathsDialog::UpdatePathsDialog(QWidget* parent, MainWindow* mainWindow)
    : QDialog(parent)
{
    this->mainWindow = mainWindow;
    ui.setupUi(this);
    ui.tableWidget->setColumnCount(3);
    ui.tableWidget->setHorizontalHeaderLabels(QStringList() << "Current Path" << "New Path" << "Similarity %");
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    ui.tableWidget->horizontalHeader()->resizeSection(2, 100);

    connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &UpdatePathsDialog::acceptDialog);
    connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(ui.algorithmComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &UpdatePathsDialog::recalculateSimilarities);
    connect(ui.autoMatchButton, &QPushButton::clicked, this, &UpdatePathsDialog::autoMatchItems);
    
    ui.algorithmComboBox->setCurrentIndex(3);

    ui.algorithmComboBox->setItemData(0, "Simple string comparison", Qt::ToolTipRole);
    ui.algorithmComboBox->setItemData(1, "Finds best partial string matches", Qt::ToolTipRole);
    ui.algorithmComboBox->setItemData(2, "Matches regardless of word order (recommended)", Qt::ToolTipRole);
    ui.algorithmComboBox->setItemData(3, "Like Token Sort but more lenient", Qt::ToolTipRole);
    
    ui.autoMatchButton->setToolTip("Find best matching files using current algorithm");

    connect(ui.tableWidget, &QTableWidget::itemDoubleClicked, this, [](QTableWidgetItem* item) {
        QApplication::clipboard()->setText(item->text());
    });
	this->filter = new WheelStrongFocusEventFilter(this);

    connect(ui.filterVideoCheckBox, &QCheckBox::stateChanged, this, [this]() {
        repopulateAllCombos();
        autoMatchItems();
    });

    connect(ui.sortByScoreCheckBox, &QCheckBox::stateChanged, this, [this]() {
        repopulateAllCombos();
    });
}

QString UpdatePathsDialog::normalizeFilename(const QString& filepath) {
    QFileInfo fileInfo(filepath);
    QString dirPath = QDir::toNativeSeparators(fileInfo.path());
    QString baseName = fileInfo.completeBaseName();

    baseName.replace(QRegularExpression("[._-]"), " ");
    baseName = baseName.toLower().trimmed();
    baseName.replace(QRegularExpression("\\s+"), " ");

    if (dirPath.isEmpty() or dirPath == ".")
        return baseName;

    return dirPath.toLower() + QDir::separator() + baseName;
}

double UpdatePathsDialog::calculateSimilarity(const QString& name1, const QString& name2) {
	QPair<QString, QString> key(normalizeFilename(name1), normalizeFilename(name2));
    
    if (similarity_cache.contains(key)) {
        return similarity_cache[key];
    }
    
    double score = 0;
    switch(ui.algorithmComboBox->currentIndex()) {
        case 0: // Simple Ratio
            score = rapidfuzz::fuzz::ratio(key.first.toStdString(), key.second.toStdString());
            break;
        case 1: // Partial Ratio
            score = rapidfuzz::fuzz::partial_ratio(key.first.toStdString(), key.second.toStdString());
            break;
        case 2: // Token Sort Ratio
            score = rapidfuzz::fuzz::token_sort_ratio(key.first.toStdString(), key.second.toStdString());
            break;
        case 3: // Token Set Ratio
            score = rapidfuzz::fuzz::token_set_ratio(key.first.toStdString(), key.second.toStdString());
            break;
        default:
            score = rapidfuzz::fuzz::ratio(key.first.toStdString(), key.second.toStdString());
    }
    
    similarity_cache[key] = score;
    return score;
}

void UpdatePathsDialog::updateSimilarityText(int row) {
    QTableWidgetItem* currentItem = ui.tableWidget->item(row, 0);
    QWidget* containerWidget = ui.tableWidget->cellWidget(row, 1);
    QTableWidgetItem* similarityItem = ui.tableWidget->item(row, 2);
    QComboBox* pathCombo = containerWidget ? containerWidget->findChild<QComboBox*>() : nullptr;

    if (currentItem && pathCombo && similarityItem) {
        QString name = InsertSettingsDialog::get_name(currentItem->text());
        QString path = pathCombo->currentText();
        double score = calculateSimilarity(name, InsertSettingsDialog::get_name(path));
        similarityItem->setText(QString::number(score, 'f', 2) + "%");
        similarityItem->setData(Qt::UserRole, score);
    }
}

void UpdatePathsDialog::repopulateCombo(int row) {
    QString currentPath = ui.tableWidget->item(row, 0)->text();
    QString name = InsertSettingsDialog::get_name(currentPath);
    QDir main_path = QDir(InsertSettingsDialog::get_maindir_path(currentPath));
    QWidget* containerWidget = ui.tableWidget->cellWidget(row, 1);
    QComboBox* pathCombo = containerWidget ? containerWidget->findChild<QComboBox*>() : nullptr;
    populateComboBox(pathCombo, currentPath, name, main_path, true);
}

void UpdatePathsDialog::repopulateAllCombos() {
    for(int row = 0; row < ui.tableWidget->rowCount(); ++row) {
        repopulateCombo(row);
    }
}

void UpdatePathsDialog::recalculateSimilarities() {
    similarity_cache.clear();

    if (ui.sortByScoreCheckBox->isChecked()) {
        repopulateAllCombos();
    }

    for(int row = 0; row < ui.tableWidget->rowCount(); ++row) {
        updateSimilarityText(row);
    }
    
    if (ui.autoMatchOnChangeCheckBox->isChecked()) {
        autoMatchItems();
    }
}

UpdatePathsDialog::~UpdatePathsDialog()
{
}

void UpdatePathsDialog::addItems(QList<QTreeWidgetItem*> items)
{
    ui.tableWidget->setRowCount(items.size());
    int row = 0;
    for (auto item : items) {
        QString currentPath = QDir::toNativeSeparators(item->text(ListColumns["PATH_COLUMN"]));
        QString name = InsertSettingsDialog::get_name(currentPath);
        QFileInfo fileInfo(currentPath);
        QDir main_path = QDir(InsertSettingsDialog::get_maindir_path(currentPath));

        QTableWidgetItem* currentItem = new QTableWidgetItem(currentPath);
        currentItem->setData(Qt::UserRole, item->data(ListColumns["PATH_COLUMN"], CustomRoles::id));
        currentItem->setData(Qt::DisplayRole, currentPath);
        currentItem->setToolTip(currentPath);
        ui.tableWidget->setItem(row, 0, currentItem);

        QWidget* pathWidget = new QWidget();
        QHBoxLayout* layout = new QHBoxLayout(pathWidget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        QComboBox* pathCombo = new QComboBox();
        pathCombo->setFocusPolicy(Qt::StrongFocus);
        pathCombo->installEventFilter(this->filter);
        pathCombo->setEditable(true);
        
        QPushButton* browseBtn = new QPushButton("...");
        browseBtn->setMaximumWidth(30);
        
        layout->addWidget(pathCombo);
        layout->addWidget(browseBtn);
        
        QTableWidgetItem* similarityItem = new QTableWidgetItem();
        similarityItem->setTextAlignment(Qt::AlignCenter);
        ui.tableWidget->setItem(row, 2, similarityItem);

        connect(pathCombo, &QComboBox::editTextChanged, this, [this, row](const QString&) {
            updateSimilarityText(row);
        });
        connect(pathCombo, &QComboBox::currentTextChanged, this, [this, row](const QString&) {
            updateSimilarityText(row);
        });

        QString browsePath = main_path.exists() ? main_path.absolutePath() : fileInfo.absolutePath();
        connect(browseBtn, &QPushButton::clicked, this, [pathCombo, browsePath]() {
            QString newPath = QFileDialog::getOpenFileName(nullptr, "Select new path", browsePath);
            if (!newPath.isEmpty()) {
                pathCombo->setCurrentText(QDir::toNativeSeparators(newPath));
            }
        });

        if (main_path.exists()) {
            populateComboBox(pathCombo, currentPath, name, main_path, false);
        }

        ui.tableWidget->setCellWidget(row, 1, pathWidget);
        row++;
    }
    autoMatchItems();
}

void UpdatePathsDialog::acceptDialog()
{
    for (int row = 0; row < ui.tableWidget->rowCount(); ++row) {
        QTableWidgetItem* currentItem = ui.tableWidget->item(row, 0);
        QWidget* containerWidget = ui.tableWidget->cellWidget(row, 1);
        QComboBox* pathCombo = nullptr;
        
        if (containerWidget) {
            pathCombo = containerWidget->findChild<QComboBox*>();
        }
        
        if (currentItem && pathCombo) {
            int videoId = currentItem->data(Qt::UserRole).toInt();
            QString newPath = QDir::toNativeSeparators(pathCombo->currentText());
            
            QFileInfo oldFile(currentItem->text());
            QFileInfo newFile(newPath);
            if (oldFile.absoluteFilePath() != newFile.absoluteFilePath()) {
                this->mainWindow->updatePath(videoId, newPath);
            }
        }
    }
    accept();
}

void UpdatePathsDialog::autoMatchItems() {
    for(int row = 0; row < ui.tableWidget->rowCount(); ++row) {
        QString currentPath = ui.tableWidget->item(row, 0)->text();
        QString name = InsertSettingsDialog::get_name(currentPath);
        QWidget* containerWidget = ui.tableWidget->cellWidget(row, 1);
        QComboBox* pathCombo = containerWidget ? containerWidget->findChild<QComboBox*>() : nullptr;
        
        if(pathCombo) {
            double best_score = 0;
            int best_index = -1;
            
            for(int i = 0; i < pathCombo->count(); i++) {
                QString filepath = pathCombo->itemText(i);
                QString filename = InsertSettingsDialog::get_name(filepath);
                double score = calculateSimilarity(name, filename);
                if(score > best_score) {
                    best_score = score;
                    best_index = i;
                }
            }
            
            if(best_index >= 0) {
                pathCombo->setCurrentIndex(best_index);
            }
        }
        updateSimilarityText(row);
    }
}

bool UpdatePathsDialog::isVideoFile(const QString& path) {
    if (!ui.filterVideoCheckBox->isChecked()) {
        return true;
    }
    QMimeType mime = mimeDb.mimeTypeForFile(path);
    return mime.name().startsWith("video/");
}

void UpdatePathsDialog::populateComboBox(QComboBox* pathCombo, const QString& currentPath, const QString& name, const QDir& main_path, bool preserveSelection) {
    if (!pathCombo || !main_path.exists()) {
        return;
    }

    QString currentText = preserveSelection ? pathCombo->currentText() : QString();
    pathCombo->clear();

    QStringList existing_files = utils::getFilesQt(main_path.absolutePath(), true, true);
    QMap<double, QStringList> scored_files;
    
    for (const QString& relative_path : existing_files) {
        QString abs_path = QDir::toNativeSeparators(main_path.absoluteFilePath(relative_path));
        if (!isVideoFile(abs_path)) {
            continue;
        }
        if (ui.sortByScoreCheckBox->isChecked()) {
            double score = calculateSimilarity(name, InsertSettingsDialog::get_name(relative_path));
            scored_files[-score].append(abs_path);
        } else {
            pathCombo->addItem(abs_path);
        }
    }
    if (ui.sortByScoreCheckBox->isChecked()) {
        for (auto it = scored_files.begin(); it != scored_files.end(); ++it) {
            for (const QString& path : it.value()) {
                pathCombo->addItem(path, -it.key());
            }
        }
    }
    if (preserveSelection) {
        int index = pathCombo->findText(currentText);
        if (index >= 0) {
            pathCombo->setCurrentIndex(index);
        }
    }
}
