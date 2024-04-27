#include "stdafx.h"
#include "importPlaylistDialog.h"

importPlaylistDialog::importPlaylistDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
}

importPlaylistDialog::~importPlaylistDialog() {
}