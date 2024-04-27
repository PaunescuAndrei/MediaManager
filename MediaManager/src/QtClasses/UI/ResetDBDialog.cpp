#include "stdafx.h"
#include "ResetDBDialog.h"
#include <QFileDialog>
#include <QPushButton>

ResetDBDialog::ResetDBDialog(QString defaultDir, QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    connect(this->ui.SetBtn,&QPushButton::clicked,this,&ResetDBDialog::set);
    connect(this->ui.AddBtn, &QPushButton::clicked, this, &ResetDBDialog::add);
    connect(this->ui.CancelBtn, &QPushButton::clicked, this, &ResetDBDialog::reject);
    this->ui.CancelBtn->setDefault(true);
    connect(this->ui.BrowseButton, &QPushButton::clicked, this, &ResetDBDialog::getDir);
    this->ui.pathText->setText(defaultDir);
}

void ResetDBDialog::getDir()
{
	QString dir = QFileDialog::getExistingDirectory(this, "Open Directory", "D:\\Vault\\Vids", QFileDialog::ShowDirsOnly);
	this->ui.pathText->setText(dir);
}

void ResetDBDialog::set()
{
    this->flag = "set";
    this->accept();
}

void ResetDBDialog::add()
{
    this->flag = "add";
    this->accept();
}

ResetDBDialog::~ResetDBDialog()
{
}
