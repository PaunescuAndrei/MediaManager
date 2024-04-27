#include "stdafx.h"
#include "setCounterDialog.h"
#include <QPushButton>
#include "stylesQt.h"

setCounterDialog::setCounterDialog(QWidget* parent) : QDialog(parent) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
	this->ui.spinBox->setPalette(get_palette("spinbox"));
	connect(this->ui.SetBtn, &QPushButton::clicked, this, &setCounterDialog::set);
	connect(this->ui.Addbtn, &QPushButton::clicked, this, &setCounterDialog::add);
	connect(this->ui.SubstractBtn, &QPushButton::clicked, this, &setCounterDialog::substract);
	connect(this->ui.CancelBtn, &QPushButton::clicked, this, &setCounterDialog::reject);
}
void setCounterDialog::set()
{
	this->flag = "set";
	this->accept();
}
void setCounterDialog::add()
{
	this->flag = "add";
	this->accept();
}
void setCounterDialog::substract()
{
	this->flag = "substract";
	this->accept();
}
setCounterDialog::~setCounterDialog() {

}