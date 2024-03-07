#include "stdafx.h"
#include "addWatchedDialog.h"
#include <QPushButton>
#include "stylesQt.h"

addWatchedDialog::addWatchedDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	ui.HH->setPalette(get_palette("spinbox"));
	ui.MM->setPalette(get_palette("spinbox"));
	ui.SS->setPalette(get_palette("spinbox"));
	connect(this->ui.Addbtn, &QPushButton::clicked, this, &addWatchedDialog::accept);
	connect(this->ui.CancelBtn, &QPushButton::clicked, this, &addWatchedDialog::reject);
}
addWatchedDialog::~addWatchedDialog() {

}