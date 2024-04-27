#include "stdafx.h"
#include "LogDialog.h"
#include "MainApp.h"
#include "logger.h"
#include <QTreeWidgetItem>
#include <QClipboard>
#include "utils.h"

LogDialog::LogDialog(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	this->setWindowModality(Qt::NonModal);
	this->layout()->setContentsMargins(0, 0, 0, 0);
	this->layout()->setSpacing(0);

	for(log_message newLog : qMainApp->logger->logs)
		this->insertNewItem(newLog.date, newLog.message, newLog.type, newLog.extra_data);

	connect(qMainApp->logger, &Logger::newLog,this, [this](log_message newLog) {
		this->insertNewItem(newLog.date, newLog.message, newLog.type, newLog.extra_data);
	});

	this->ui.treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this->ui.treeWidget, &QTreeWidget::customContextMenuRequested, this, [this](QPoint point) {
		QModelIndex index = this->ui.treeWidget->indexAt(point);
		if (!index.isValid())
			return;
		QTreeWidgetItem* item = this->ui.treeWidget->itemAt(point);
		QMenu menu = QMenu(this);
		menu.setObjectName("LogQMenuClass");
		QAction* open_fileexp = new QAction("Open File Location", &menu);
		menu.addAction(open_fileexp);
		QAction* menu_click = menu.exec(this->ui.treeWidget->viewport()->mapToGlobal(point));
		if (!menu_click)
			return;
		if (menu_click == open_fileexp) {
			utils::openFileExplorer(item->data(0,Qt::UserRole).toString());
		}
	});

	connect(this->ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
		QString extra_data = item->data(0, Qt::UserRole).toString();
		if (!extra_data.isEmpty()) {
			qMainApp->clipboard()->setText(extra_data);
			if (item->text(1) == "SoundEffect") {
				qMainApp->soundPlayer->play(extra_data, false);
			}
		}
		else
			qMainApp->clipboard()->setText(item->text(2));
	});
}

void LogDialog::insertNewItem(QString Date, QString Message, QString Type, QString Extra_Data) {
	auto bar = this->ui.treeWidget->verticalScrollBar();
	bool viewAtBottom = bar ? (bar->value() == bar->maximum()) : false;
	if(this->ui.treeWidget->topLevelItemCount() >= qMainApp->logger->max_size)
		delete this->ui.treeWidget->takeTopLevelItem(0);
	QTreeWidgetItem* item = new QTreeWidgetItem({ Date, Type, Message });
	item->setData(0, Qt::UserRole, Extra_Data);
	this->ui.treeWidget->addTopLevelItem(item);
	this->ui.treeWidget->resizeColumnToContents(0);
	this->ui.treeWidget->resizeColumnToContents(1);
	if (viewAtBottom)
		this->ui.treeWidget->scrollToBottom();
}

LogDialog::~LogDialog()
{
}
