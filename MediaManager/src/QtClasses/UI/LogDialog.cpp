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

	logTimer = new QTimer(this);
	logTimer->setInterval(100); // Process logs every 100ms
	connect(logTimer, &QTimer::timeout, this, &LogDialog::processBufferedLogs);

	for(log_message newLog : qMainApp->logger->getLogs())
		this->insertNewItem(newLog.date, newLog.message, newLog.type, newLog.extra_data);

	connect(qMainApp->logger, &Logger::newLog,this, [this](log_message newLog) {
		logBuffer.append(newLog);
		if (!logTimer->isActive()) {
			logTimer->start();
		}
	});

	this->ui.treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this->ui.treeWidget, &QTreeWidget::customContextMenuRequested, this, [this](QPoint point) {
        const QPersistentModelIndex index(this->ui.treeWidget->indexAt(point));
        if (!index.isValid())
            return;
        QTreeWidgetItem* item = this->ui.treeWidget->itemFromIndex(index);
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
				qMainApp->soundPlayer->play(extra_data, true, false);
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
	if (viewAtBottom)
		this->ui.treeWidget->scrollToBottom();
}

void LogDialog::processBufferedLogs() {
	logTimer->stop();
	this->ui.treeWidget->setUpdatesEnabled(false);
	this->ui.treeWidget->clear(); // Clear all existing items
	QList<log_message> currentLogs = qMainApp->logger->getLogs();
	for (const log_message& newLog : currentLogs) {
		// Re-add all logs from the logger's main list
		QTreeWidgetItem* item = new QTreeWidgetItem({ newLog.date, newLog.type, newLog.message });
		item->setData(0, Qt::UserRole, newLog.extra_data);
		this->ui.treeWidget->addTopLevelItem(item);
	}
	logBuffer.clear();
	this->ui.treeWidget->resizeColumnToContents(0);
	this->ui.treeWidget->resizeColumnToContents(1);
	this->ui.treeWidget->setUpdatesEnabled(true);
	// Scroll to bottom if it was at the bottom before clearing
	auto bar = this->ui.treeWidget->verticalScrollBar();
	if (bar && bar->maximum() > 0) {
		this->ui.treeWidget->scrollToBottom();
	}
}

LogDialog::~LogDialog()
{
	delete logTimer;
}
