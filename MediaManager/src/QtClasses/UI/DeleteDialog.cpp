#include "stdafx.h"
#include "DeleteDialog.h"

DeleteDialog::DeleteDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
}

DeleteDialog::~DeleteDialog() {

}