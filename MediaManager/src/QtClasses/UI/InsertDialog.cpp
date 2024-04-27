#include "stdafx.h"
#include "InsertDialog.h"
#include <QFileDialog>
#include <QStringList>
#include <QDir>

InsertDialog::InsertDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
	connect(this->ui.BrowseButton, &QPushButton::clicked,this,&InsertDialog::getFile);
}

void InsertDialog::getFile() {
	QStringList fileNames = QFileDialog::getOpenFileNames(this, "Insert Files", "D:\\Vault\\Vids");
	for (QString& file : fileNames) {
		file = QDir::toNativeSeparators(file);
	}
	this->ui.pathText->setText(fileNames.join(" , "));
}

InsertDialog::~InsertDialog() {

}