#include "stdafx.h"
#include "InsertSettingsDialog.h"
#include <QFileDialog>
#include <QStringList>
#include <QDir>
#include "definitions.h"
#include "utils.h"
#include <QStringList>
#include "AutoToolTipDelegate.h"
#include "stylesQt.h"

InsertSettingsDialog::InsertSettingsDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->ui.nameDirComboBox->addItem("Auto");
	this->ui.nameDirComboBox->setCurrentIndex(0);
	this->ui.authorComboBox->addItem("");
	this->ui.authorComboBox->addItem("Auto");
	this->ui.authorComboBox->setCurrentIndex(1);
	this->ui.typeComboBox->addItem("");
	this->ui.typeComboBox->addItem("Auto");
	this->ui.typeComboBox->setCurrentIndex(1);

	connect(this->ui.nameDirComboBox, &QComboBox::currentTextChanged, [this](const QString& text) {
		this->update_treewidget(2);
	});
	connect(this->ui.authorComboBox, &QComboBox::currentTextChanged, [this](const QString& text) {
		this->update_treewidget(1);
	});
	connect(this->ui.typeComboBox, &QComboBox::currentTextChanged, [this](const QString& text) {
		this->update_treewidget(3);
	});

	this->ui.newFilesTreeWidget->setItemDelegate(new AutoToolTipDelegate(this->ui.newFilesTreeWidget));
	this->ui.newFilesTreeWidget->setStyleSheet(get_stylesheet("videoswidget"));
	this->ui.newFilesTreeWidget->sortItems(0, Qt::SortOrder::AscendingOrder);
	auto header = this->ui.newFilesTreeWidget->header();
	header->setSectionResizeMode(QHeaderView::ResizeToContents);
	header->setStretchLastSection(false);
	header->setSectionResizeMode(0, QHeaderView::Interactive);
	header->setSectionResizeMode(2, QHeaderView::Stretch);
}

QString InsertSettingsDialog::get_author(QString path) {
	QStringList dirs = utils::getDirNames(path);
	int index = -1;
	int i = 0;
	for (QString& dir : dirs) {
		if (videoTypes.contains(dir, Qt::CaseInsensitive)) {
			if(i > 0)
				index = i - 1;
			break;
		}
		i++;
	}
	if(index < 0)
		return "";
	return dirs.at(index);
}

QString InsertSettingsDialog::get_namedir(QString path) {
	QStringList dirs = utils::getDirNames(path);
	int index = 0;
	int i = 0;
	for (QString& dir : dirs) {
		if (videoTypes.contains(dir, Qt::CaseInsensitive)) {
			if (i > 0)
				index = i - 1;
			break;
		}
		i++;
	}
	return dirs.at(index);
}

QString InsertSettingsDialog::get_name(QString path,QString dirsplit) {
	QStringList dirs = utils::getDirNames(path);
	QString namedir;
	if (dirsplit.isEmpty())
		namedir = InsertSettingsDialog::get_namedir(path);
	else
		namedir = dirsplit;
	int index = dirs.indexOf(namedir);
	const QFileInfo info(path);
	QString name = info.fileName();
	for (int i = 0; i < index; i++) {
		name = utils::pathAppend(dirs.at(i), name);
	}
	return name;
}

QString InsertSettingsDialog::get_type(QString path) {
	QStringList dirs = utils::getDirNames(path);
	QString type = "";
	for (QString& video_type : videoTypes) {
		if (dirs.contains(video_type,Qt::CaseInsensitive)) {
			type = video_type;
			break;
		}
	}
	return type;
}

void InsertSettingsDialog::init_namedir(QStringList items) {
	this->ui.nameDirComboBox->addItems(items);
}

void InsertSettingsDialog::init_namedir(QString path) {
	this->ui.nameDirComboBox->addItems(utils::getDirNames(path));
}

void InsertSettingsDialog::init_authors(QStringList items) {
	this->ui.authorComboBox->addItems(items);
}

void InsertSettingsDialog::init_authors(QString path) {
	this->ui.authorComboBox->addItems(utils::getDirNames(path));
}

void InsertSettingsDialog::init_types(QStringList items) {
	this->ui.typeComboBox->addItems(items);
}

void InsertSettingsDialog::init_types() {
	for (QString& t : videoTypes) {
		this->ui.typeComboBox->addItem(t);
	}
}

void InsertSettingsDialog::update_treewidget_author(QTreeWidgetItem* item) {
	if (this->ui.authorComboBox->currentText() == "Auto") {
		item->setText(1, this->get_author(item->text(0)));
	}
	else if (this->ui.authorComboBox->currentText() != item->text(1)) {
		item->setText(1, this->ui.authorComboBox->currentText());
	}
}

void InsertSettingsDialog::update_treewidget_name(QTreeWidgetItem* item) {
	if (this->ui.nameDirComboBox->currentText() == "Auto") {
		item->setText(2, this->get_name(item->text(0)));
	}
	else {
		item->setText(2, this->get_name(item->text(0), this->ui.nameDirComboBox->currentText()));
	}
}

void InsertSettingsDialog::update_treewidget_type(QTreeWidgetItem* item) {
	if (this->ui.typeComboBox->currentText() == "Auto") {
		item->setText(3, this->get_type(item->text(0)));
	}
	else if (this->ui.typeComboBox->currentText() != item->text(3)) {
		item->setText(3, this->ui.typeComboBox->currentText());
	}
}

void InsertSettingsDialog::update_treewidget() {
	QTreeWidgetItemIterator it(this->ui.newFilesTreeWidget);
	while (*it) {
		this->update_treewidget_author((*it));
		this->update_treewidget_name((*it));
		this->update_treewidget_type((*it));
		++it;
	}
}

void InsertSettingsDialog::update_treewidget(int col) {
	QTreeWidgetItemIterator it(this->ui.newFilesTreeWidget);
	while (*it) {
		if (col == 1) {
			this->update_treewidget_author((*it));
		}
		else if (col == 2) {
			this->update_treewidget_name((*it));
		}
		else if (col == 3) {
			this->update_treewidget_type((*it));
		}
		++it;
	}
}

InsertSettingsDialog::~InsertSettingsDialog() {

}