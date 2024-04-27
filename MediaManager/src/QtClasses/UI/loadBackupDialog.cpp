#include "stdafx.h"
#include "loadBackupDialog.h"

loadBackupDialog::loadBackupDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
}

loadBackupDialog::~loadBackupDialog() {
}