#include "stdafx.h"
#include "InsertSettingsDialog.h"
#include <QFileDialog>
#include <QStringList>
#include <QDir>
#include "definitions.h"
#include "utils.h"
#include <QStringList>
#include <QSignalBlocker>
#include <QGuiApplication>
#include "AutoToolTipDelegate.h"
#include "stylesQt.h"
#include <filesystem>
#include "MainWindow.h"
#include "MainApp.h"
#include "VideosTagsDialog.h"
#include "PreviewTooltipDialog.h"
#include "VideoPreviewWidget.h"

InsertSettingsDialog::InsertSettingsDialog(MainWindow* mainWindow, QWidget* parent) : QDialog(parent), MW(mainWindow) {
	ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
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
	connect(this->ui.showExistingCheckBox, &QCheckBox::checkStateChanged, [this](Qt::CheckState state) {
		QTreeWidgetItemIterator it = QTreeWidgetItemIterator(this->ui.newFilesTreeWidget);
		while (*it) {
			if (state == Qt::Checked) {
				(*it)->setHidden(false);
			}
			else {
				if((*it)->data(0, CustomRoles::id).toInt() != -1)
					(*it)->setHidden(true);
				else
					(*it)->setHidden(false);
			}
			++it;
		}
	});

	this->ui.newFilesTreeWidget->setItemDelegate(new AutoToolTipDelegate(this->ui.newFilesTreeWidget));
	this->ui.newFilesTreeWidget->setStyleSheet(get_stylesheet("videoswidget"));
	this->ui.newFilesTreeWidget->sortItems(0, Qt::SortOrder::AscendingOrder);
	auto header = this->ui.newFilesTreeWidget->header();
	header->setSectionResizeMode(QHeaderView::ResizeToContents);
	header->setStretchLastSection(false);
	header->setSectionResizeMode(0, QHeaderView::Interactive);
	header->setSectionResizeMode(2, QHeaderView::Stretch);
	header->setSectionResizeMode(4, QHeaderView::Interactive);
	header->resizeSection(4, 220);
	this->setupTreeEditing();
}

void InsertSettingsDialog::setupTreeEditing() {
	this->ui.newFilesTreeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	this->ui.newFilesTreeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
	this->ui.newFilesTreeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
	this->ui.newFilesTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this->ui.newFilesTreeWidget, &QTreeWidget::itemDoubleClicked, this, &InsertSettingsDialog::onItemDoubleClicked);
	connect(this->ui.newFilesTreeWidget, &QTreeWidget::itemChanged, this, &InsertSettingsDialog::onItemChanged);
	connect(this->ui.newFilesTreeWidget, &QTreeWidget::customContextMenuRequested, this, &InsertSettingsDialog::onTreeContextMenu);
	this->ui.newFilesTreeWidget->viewport()->installEventFilter(this);
}

void InsertSettingsDialog::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
	if (!item || item->isDisabled())
		return;
	if (column >= 1 && column <= 3) {
		this->ui.newFilesTreeWidget->editItem(item, column);
	}
	else if (column == 4) {
		this->editRowTags(item);
	}
}

void InsertSettingsDialog::onItemChanged(QTreeWidgetItem* item, int column) {
	if (!item || this->updatingTree)
		return;
	if (column < 1 || column > 3)
		return;
	this->updatingTree = true;
	if (column == 1)
		item->setData(1, InsertRoles::authorOverridden, true);
	else if (column == 2)
		item->setData(2, InsertRoles::nameOverridden, true);
	else if (column == 3)
		item->setData(3, InsertRoles::typeOverridden, true);
	this->updatingTree = false;
}

void InsertSettingsDialog::onTreeContextMenu(const QPoint& point) {
	QTreeWidgetItem* clicked = this->ui.newFilesTreeWidget->itemAt(point);
	if (!clicked)
		return;

	// Right-clicking a row outside the current selection makes it the new selection.
	// Right-clicking inside an existing multi-selection keeps that selection intact.
	if (!clicked->isSelected()) {
		this->ui.newFilesTreeWidget->clearSelection();
		this->ui.newFilesTreeWidget->setCurrentItem(clicked);
		clicked->setSelected(true);
	}

	const QList<QTreeWidgetItem*> selected = this->ui.newFilesTreeWidget->selectedItems();
	QList<QTreeWidgetItem*> editable;
	editable.reserve(selected.size());
	for (QTreeWidgetItem* item : selected) {
		if (item && !item->isDisabled())
			editable.append(item);
	}

	QMenu menu(this);
	QAction* previewAction = menu.addAction("Preview");
	QAction* editTagsAction = nullptr;
	QAction* clearTagsAction = nullptr;
	QAction* resetAction = nullptr;
	if (!editable.isEmpty()) {
		menu.addSeparator();
		if (selected.size() > 1) {
			QString countText = QStringLiteral("%1 rows selected").arg(static_cast<int>(selected.size()));
			if (editable.size() != selected.size())
				countText += QStringLiteral(" (%1 editable)").arg(static_cast<int>(editable.size()));
			QAction* countAction = menu.addAction(countText);
			countAction->setEnabled(false);
		}
		editTagsAction = menu.addAction("Edit tags…");
		clearTagsAction = menu.addAction("Clear tags");
		resetAction = menu.addAction("Reset fields to auto");
	}

	QAction* chosen = menu.exec(this->ui.newFilesTreeWidget->viewport()->mapToGlobal(point));
	if (!chosen)
		return;
	if (chosen == previewAction)
		this->showRowPreview(clicked);
	else if (chosen == editTagsAction)
		this->editRowsTags(editable);
	else if (chosen == clearTagsAction)
		this->clearRowsTags(editable);
	else if (chosen == resetAction)
		this->resetRowsOverrides(editable);
}

bool InsertSettingsDialog::eventFilter(QObject* obj, QEvent* event) {
	if (obj == this->ui.newFilesTreeWidget->viewport() && event->type() == QEvent::MouseButtonPress) {
		auto* mouse = static_cast<QMouseEvent*>(event);
		if ((mouse->button() == Qt::LeftButton || mouse->button() == Qt::RightButton || mouse->button() == Qt::MiddleButton) && (mouse->modifiers() & Qt::AltModifier)) {
			QTreeWidgetItem* item = this->ui.newFilesTreeWidget->itemAt(mouse->position().toPoint());
			if (item) {
				this->showRowPreview(item);
				return true;
			}
		}
	}
	return QDialog::eventFilter(obj, event);
}

void InsertSettingsDialog::closeEvent(QCloseEvent* event) {
	if (this->rowPreview) {
		this->rowPreview->close();
		this->rowPreview = nullptr;
	}
	QDialog::closeEvent(event);
}

void InsertSettingsDialog::done(int result) {
	if (this->rowPreview) {
		this->rowPreview->close();
		this->rowPreview = nullptr;
	}
	QDialog::done(result);
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

QString InsertSettingsDialog::get_maindir_path(QString path, QString dirsplit)
{
	QString rootPath = utils::getRootPath(path);
	QStringList dirs = utils::getDirNames(path);
	QString namedir;
	if (dirsplit.isEmpty())
		namedir = InsertSettingsDialog::get_namedir(path);
	else
		namedir = dirsplit;
	int index = dirs.indexOf(namedir);
	const QFileInfo info(path);
	//QString name = info.fileName();
	QString name;
	for (int i = index; i < dirs.length(); i++) {
		name = utils::pathAppend(dirs.at(i), name);
	}
	name = rootPath % name;
	return name;
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
	if (!item || item->data(1, InsertRoles::authorOverridden).toBool())
		return;
	if (this->ui.authorComboBox->currentText() == "Auto") {
		item->setText(1, this->get_author(item->text(0)));
	}
	else if (this->ui.authorComboBox->currentText() != item->text(1)) {
		item->setText(1, this->ui.authorComboBox->currentText());
	}
}

void InsertSettingsDialog::update_treewidget_name(QTreeWidgetItem* item) {
	if (!item || item->data(2, InsertRoles::nameOverridden).toBool())
		return;
	if (this->ui.nameDirComboBox->currentText() == "Auto") {
		item->setText(2, this->get_name(item->text(0)));
	}
	else {
		item->setText(2, this->get_name(item->text(0), this->ui.nameDirComboBox->currentText()));
	}
}

void InsertSettingsDialog::update_treewidget_type(QTreeWidgetItem* item) {
	if (!item || item->data(3, InsertRoles::typeOverridden).toBool())
		return;
	if (this->ui.typeComboBox->currentText() == "Auto") {
		item->setText(3, this->get_type(item->text(0)));
	}
	else if (this->ui.typeComboBox->currentText() != item->text(3)) {
		item->setText(3, this->ui.typeComboBox->currentText());
	}
}

void InsertSettingsDialog::update_treewidget() {
	// Bulk refresh must not be recorded as manual edits on the rows.
	const QSignalBlocker blocker(this->ui.newFilesTreeWidget);
	QTreeWidgetItemIterator it(this->ui.newFilesTreeWidget);
	while (*it) {
		this->update_treewidget_author((*it));
		this->update_treewidget_name((*it));
		this->update_treewidget_type((*it));
		++it;
	}
	this->updatingTree = false;
}

void InsertSettingsDialog::update_treewidget(int col) {
	const QSignalBlocker blocker(this->ui.newFilesTreeWidget);
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

QSet<int> InsertSettingsDialog::itemTagIds(QTreeWidgetItem* item) const {
	QSet<int> ids;
	if (!item)
		return ids;
	const QVariantList idList = item->data(4, InsertRoles::tagIds).toList();
	for (const QVariant& value : idList) {
		ids.insert(value.toInt());
	}
	return ids;
}

QString InsertSettingsDialog::tagIdsDisplayText(const QSet<int>& ids) const {
	if (ids.isEmpty())
		return QString();
	if (!this->MW || !this->MW->App || !this->MW->App->db)
		return QString();

	const QSet<Tag> allTags = this->MW->App->db->getAllTags();
	QList<Tag> selected;
	selected.reserve(ids.size());
	for (const Tag& tag : allTags) {
		if (ids.contains(tag.id))
			selected.append(tag);
	}
	std::sort(selected.begin(), selected.end(), [](const Tag& lhs, const Tag& rhs) {
		if (lhs.display_priority == rhs.display_priority)
			return lhs.id < rhs.id;
		return lhs.display_priority < rhs.display_priority;
	});
	QStringList names;
	names.reserve(selected.size());
	for (const Tag& tag : selected)
		names.append(tag.name);
	return names.join(", ");
}

void InsertSettingsDialog::setRowsTagIds(const QList<QTreeWidgetItem*>& items, const QSet<int>& ids) {
	if (items.isEmpty())
		return;

	QVariantList idList;
	idList.reserve(ids.size());
	for (int id : ids)
		idList.append(id);

	const QString text = this->tagIdsDisplayText(ids);
	const QSignalBlocker blocker(this->ui.newFilesTreeWidget);
	for (QTreeWidgetItem* item : items) {
		if (!item || item->isDisabled())
			continue;
		item->setData(4, InsertRoles::tagIds, idList);
		item->setData(4, InsertRoles::tagsOverridden, true);
		item->setText(4, text);
		item->setToolTip(4, text);
	}
}

void InsertSettingsDialog::setItemTagIds(QTreeWidgetItem* item, const QSet<int>& ids) {
	if (!item)
		return;
	this->setRowsTagIds({ item }, ids);
}

void InsertSettingsDialog::editRowTags(QTreeWidgetItem* item) {
	if (!item || item->isDisabled() || !this->MW)
		return;
	this->editRowsTags({ item });
}

void InsertSettingsDialog::editRowsTags(const QList<QTreeWidgetItem*>& items) {
	if (!this->MW)
		return;

	QList<QTreeWidgetItem*> valid;
	valid.reserve(items.size());
	for (QTreeWidgetItem* item : items) {
		if (item && !item->isDisabled())
			valid.append(item);
	}
	if (valid.isEmpty())
		return;

	QSet<int> common = this->itemTagIds(valid.first());
	bool mixed = false;
	for (int i = 1; i < valid.size(); ++i) {
		if (this->itemTagIds(valid[i]) != common) {
			mixed = true;
			break;
		}
	}

	VideosTagsDialog dialog(mixed ? QSet<int>() : common, this->MW, this);
	if (valid.size() > 1 || mixed) {
		QString title = QStringLiteral("Video Tags");
		if (valid.size() > 1)
			title += QStringLiteral(" (%1 rows)").arg(static_cast<int>(valid.size()));
		if (mixed)
			title += QStringLiteral(" - mixed tags");
		dialog.setWindowTitle(title);
	}

	if (dialog.exec() == QDialog::Accepted) {
		this->setRowsTagIds(valid, dialog.selectedTagIds());
	}
}

void InsertSettingsDialog::clearRowsTags(const QList<QTreeWidgetItem*>& items) {
	this->setRowsTagIds(items, QSet<int>());
}

void InsertSettingsDialog::showRowPreview(QTreeWidgetItem* item) {
	if (!item || !this->MW || !this->MW->App)
		return;
	const QString path = item->text(0);
	if (path.isEmpty() || !QFileInfo::exists(path))
		return;

	if (this->rowPreview) {
		this->rowPreview->close();
		this->rowPreview = nullptr;
	}

	auto* popup = this->MW->createPreviewPopup(path, this, false, false);
	if (!popup)
		return;
	popup->setProperty("storeOnClose", false);
	this->rowPreview = popup;

	QRect screenRect;
	const QPoint anchor = this->ui.newFilesTreeWidget->viewport()->mapToGlobal(this->ui.newFilesTreeWidget->visualItemRect(item).topRight());
	if (QScreen* screen = QGuiApplication::screenAt(anchor)) {
		screenRect = screen->availableGeometry();
	}
	else if (QScreen* primary = QGuiApplication::primaryScreen()) {
		screenRect = primary->availableGeometry();
	}

	const int screenMaxW = screenRect.isValid() ? screenRect.width() : 1920;
	const int screenMaxH = screenRect.isValid() ? screenRect.height() : 1080;
	const int popupW = qBound(160, this->MW->App->config->get("preview_main_width").toInt(), screenMaxW);
	const int popupH = qBound(120, this->MW->App->config->get("preview_main_height").toInt(), screenMaxH);
	popup->resize(popupW, popupH);

	int x = anchor.x() + 12;
	int y = anchor.y() - popupH / 2;
	if (screenRect.isValid()) {
		if (x + popupW > screenRect.right())
			x = anchor.x() - popupW - 12;
		x = std::clamp(x, screenRect.left(), std::max(screenRect.left(), screenRect.right() - popupW));
		y = std::clamp(y, screenRect.top(), std::max(screenRect.top(), screenRect.bottom() - popupH));
	}
	popup->move(x, y);
	// Cloak the popup until its first paint to suppress the Windows first-show ghost.
	utils::cloak_until_first_paint(popup);
	popup->show();
	if (VideoPreviewWidget* preview = popup->previewWidget()) {
		preview->prepareInitialFrame(true);
	}
}

void InsertSettingsDialog::resetRowOverrides(QTreeWidgetItem* item) {
	if (!item)
		return;
	{
		const QSignalBlocker blocker(this->ui.newFilesTreeWidget);
		item->setData(1, InsertRoles::authorOverridden, false);
		item->setData(2, InsertRoles::nameOverridden, false);
		item->setData(3, InsertRoles::typeOverridden, false);
	}
	{
		const QSignalBlocker blocker(this->ui.newFilesTreeWidget);
		this->update_treewidget_author(item);
		this->update_treewidget_name(item);
		this->update_treewidget_type(item);
	}
}

void InsertSettingsDialog::resetRowsOverrides(const QList<QTreeWidgetItem*>& items) {
	for (QTreeWidgetItem* item : items) {
		if (item && !item->isDisabled())
			this->resetRowOverrides(item);
	}
}

InsertSettingsDialog::~InsertSettingsDialog() {
	if (this->rowPreview) {
		this->rowPreview->close();
		this->rowPreview = nullptr;
	}
}
