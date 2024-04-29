#include "stdafx.h"
#include "FilterDialog.h"
#include <QFileDialog>
#include <QStringList>
#include <QDir>
#include "flowlayout.h"
#include <QCheckBox>
#include "MainWindow.h"
#include "MainApp.h"
#include "FilterSettings.h"
#include <QMap>
#include <QJsonValue>
#include "stylesQt.h"
#include "utils.h"

FilterDialog::FilterDialog(MainWindow* MW, QJsonObject settings, QWidget* parent) : QDialog(parent) {
    this->resize(this->width(), 444);
	ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    if (settings.isEmpty())
        settings = QJsonObject(MW->filterSettings.json);
    this->ui.viewsSpinBox->setStyleSheet(get_stylesheet("spinbox"));
    this->ui.ratingSpinBox->setStyleSheet(get_stylesheet("doublespinbox"));
    //Types
    FlowLayout* types_flowLayout = new FlowLayout;
    types_flowLayout->setContentsMargins(0, 0, 0, 0);
    QStringList types_include = MW->App->db->getTypes(MW->App->currentDB);
    QMap<QString, QCheckBox*> types_map = QMap<QString, QCheckBox*>();
    this->all_types = new QCheckBox("All", this);
    types_map["All"] = all_types;
    connect(all_types, &QCheckBox::checkStateChanged, this, &FilterDialog::typesChanged);
    types_flowLayout->addWidget(all_types);
    for (QString& type : types_include) {
        QCheckBox* checkbox;
        if (type == "")
            checkbox = new QCheckBox("None", this);
        else
            checkbox = new QCheckBox(type, this);
        types_map[type] = checkbox;
        connect(checkbox, &QCheckBox::checkStateChanged, this, &FilterDialog::typesChanged);
        types_flowLayout->addWidget(checkbox);
    }
    this->ui.types->setLayout(types_flowLayout);
    //Authors
    FlowLayout* authors_flowLayout = new FlowLayout;
    authors_flowLayout->setContentsMargins(0, 0, 0, 0);
    QStringList authors = MW->App->db->getAuthors(MW->App->currentDB);
    QMap<QString, QCheckBox*> authors_map = QMap<QString, QCheckBox*>();
    this->all_authors = new QCheckBox("All", this);
    authors_map["All"] = all_authors;
    connect(all_authors, &QCheckBox::checkStateChanged, this, &FilterDialog::authorsChanged);
    authors_flowLayout->addWidget(all_authors);
    for (QString& author : authors){
        QCheckBox* checkbox;
        if (author == "")
            checkbox = new QCheckBox("None",this);
        else
            checkbox = new QCheckBox(author, this);
        authors_map[author] = checkbox;
        connect(checkbox, &QCheckBox::checkStateChanged, this, &FilterDialog::authorsChanged);
        authors_flowLayout->addWidget(checkbox);
    }
    this->ui.authors->setLayout(authors_flowLayout);

    //Tags
    FlowLayout* tags_flowLayout = new FlowLayout;
    tags_flowLayout->setContentsMargins(0, 0, 0, 0);
    QSet<Tag> tags = MW->App->db->getAllTags();
    QMap<QString, QCheckBox*> tags_map = QMap<QString, QCheckBox*>();
    this->all_tags = new QCheckBox("All", this);
    QCheckBox* no_tags= new QCheckBox("None", this);
    tags_map["All"] = this->all_tags;
    tags_map["None"] = no_tags;
    connect(this->all_tags, &QCheckBox::checkStateChanged, this, &FilterDialog::tagsChanged);
    connect(no_tags, &QCheckBox::checkStateChanged, this, &FilterDialog::tagsChanged);
    tags_flowLayout->addWidget(this->all_tags);
    tags_flowLayout->addWidget(no_tags);
    for (const Tag& tag : tags) {
        QCheckBox* checkbox;
        checkbox = new QCheckBox(tag.name, this);
        checkbox->setProperty("tag", QVariant::fromValue(tag));
        tags_map[QString::number(tag.id)] = checkbox;
        connect(checkbox, &QCheckBox::checkStateChanged, this, &FilterDialog::tagsChanged);
        tags_flowLayout->addWidget(checkbox);
    }
    this->ui.tags->setLayout(tags_flowLayout);

    //init values from settings
    QJsonArray authors_arr = settings.value("authors").toArray();
    for (QJsonValue auth : authors_arr) {
        QCheckBox* chckbox = authors_map.value(auth.toString(), nullptr);
        if (chckbox)
            chckbox->setChecked(true);
    }
    QJsonArray tags_arr = settings.value("tags").toArray();
    for (QJsonValue tag : tags_arr) {
        QCheckBox* chckbox = tags_map.value(tag.toString(), nullptr);
        if (chckbox)
            chckbox->setChecked(true);
    }
    QJsonArray types_arr = settings.value("types_include").toArray();
    for (QJsonValue typ : types_arr) {
        QCheckBox* chckbox = types_map.value(typ.toString(), nullptr);
        if (chckbox)
            chckbox->setChecked(true);
    }
    QJsonArray watched_arr = settings.value("watched").toArray();
    for (QJsonValue typ : watched_arr) {
        if (typ == "All") {
            this->ui.watched_yes->setChecked(true);
            this->ui.watched_no->setChecked(true);
        }
        else if (typ == "Yes") {
            this->ui.watched_yes->setChecked(true);
        }
        else if (typ == "No") {
            this->ui.watched_no->setChecked(true);
        }
    }
    this->ui.viewsComboBox->setCurrentIndex(this->ui.viewsComboBox->findText(settings.value("views_mode").toString()));
    this->ui.viewsSpinBox->setValue(settings.value("views").toString().toInt());
    this->ui.ratingComboBox->setCurrentIndex(this->ui.ratingComboBox->findText(settings.value("rating_mode").toString()));
    this->ui.ratingSpinBox->setValue(settings.value("rating").toString().toDouble());
    this->ui.ignore_defaults->setChecked(utils::text_to_bool(settings.value("ignore_defaults").toString().toStdString()));
    this->ui.include_unrated->setChecked(utils::text_to_bool(settings.value("include_unrated").toString().toStdString()));

    QList<QSpinBox*> spinboxes = this->findChildren<QSpinBox*>();
    for (auto spinbox : spinboxes) {
        spinbox->installEventFilter(this);
    }
    QList<QDoubleSpinBox*> doublespinboxes = this->findChildren<QDoubleSpinBox*>();
    for (auto doublespinbox : doublespinboxes) {
        doublespinbox->installEventFilter(this);
    }
    QList<QComboBox*> comboboxes = this->findChildren<QComboBox*>();
    for (auto combobox : comboboxes) {
        combobox->installEventFilter(this);
    }
    this->setFocus();
}

void FilterDialog::authorsChanged(Qt::CheckState state) {
    QCheckBox* checkbox_sender = qobject_cast<QCheckBox*>(sender());
    if (state == Qt::Checked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_authors = this->ui.authors->findChildren<QCheckBox*>();
            all_authors.removeOne(checkbox_sender);
            for (QCheckBox* author : all_authors) {
                author->setChecked(true);
            }
        }
    }
    else if (state == Qt::Unchecked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_authors = this->ui.authors->findChildren<QCheckBox*>();
            all_authors.removeOne(checkbox_sender);
            for (QCheckBox* author : all_authors) {
                author->setChecked(false);
            }
        }
        else {
            if (this->all_authors->isChecked()) {
                bool oldState = this->all_authors->blockSignals(true);
                this->all_authors->setChecked(false);
                this->all_authors->blockSignals(oldState);
            }
        }
    }
}

void FilterDialog::tagsChanged(Qt::CheckState state) {
    QCheckBox* checkbox_sender = qobject_cast<QCheckBox*>(sender());
    if (state == Qt::Checked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_tags = this->ui.tags->findChildren<QCheckBox*>();
            all_tags.removeOne(checkbox_sender);
            for (QCheckBox* tag : all_tags) {
                tag->setChecked(true);
            }
        }
    }
    else if (state == Qt::Unchecked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_tags = this->ui.tags->findChildren<QCheckBox*>();
            all_tags.removeOne(checkbox_sender);
            for (QCheckBox* tag : all_tags) {
                tag->setChecked(false);
            }
        }
        else {
            if (this->all_tags->isChecked()) {
                bool oldState = this->all_tags->blockSignals(true);
                this->all_tags->setChecked(false);
                this->all_tags->blockSignals(oldState);
            }
        }
    }
}

void FilterDialog::typesChanged(Qt::CheckState state) {
    QCheckBox* checkbox_sender = qobject_cast<QCheckBox*>(sender());
    if (state == Qt::Checked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_types = this->ui.types->findChildren<QCheckBox*>();
            all_types.removeOne(checkbox_sender);
            for (QCheckBox* type : all_types) {
                type->setChecked(true);
            }
        }
    }
    else if (state == Qt::Unchecked) {
        if (checkbox_sender->text() == "All") {
            QList<QCheckBox*> all_types = this->ui.types->findChildren<QCheckBox*>();
            all_types.removeOne(checkbox_sender);
            for (QCheckBox* type : all_types) {
                type->setChecked(false);
            }
        }
        else {
            if (this->all_types->isChecked()) {
                bool oldState = this->all_types->blockSignals(true);
                this->all_types->setChecked(false);
                this->all_types->blockSignals(oldState);
            }
        }
    }
}

QJsonObject FilterDialog::toJson()
{
    QJsonObject json = QJsonObject();
    //Types
    QList<QCheckBox*> all_types = this->ui.types->findChildren<QCheckBox*>();
    QJsonArray types_include = {};
    for (QCheckBox* type : all_types) {
        if (type->isChecked()) {
            if (type->text() == "All") {
                types_include = { "All" };
                break;
            }
            else {
                if(type->text() == "None")
                    types_include.append("");
                else
                    types_include.append(type->text());
            }
        }
    }
    json.insert("types_include", types_include);
    //Authors
    QList<QCheckBox*> all_authors = this->ui.authors->findChildren<QCheckBox*>();
    QJsonArray authors = {};
    for (QCheckBox* author : all_authors) {
        if (author->isChecked()) {
            if (author->text() == "All") {
                authors = { "All" };
                break;
            }
            else {
                if (author->text() == "None")
                    authors.append("");
                else
                    authors.append(author->text());
            }
        }
    }
    json.insert("authors", authors);
    //Tags
    QList<QCheckBox*> all_tags = this->ui.tags->findChildren<QCheckBox*>();
    QJsonArray tags = {};
    for (QCheckBox* tag : all_tags) {
        if (tag->isChecked()) {
            if (tag->text() == "All") {
                tags = { "All" };
                break;
            }else if (tag->text() == "None") {
                tags.append(tag->text());
                continue;
            }
            else {
                Tag t = tag->property("tag").value<Tag>();
                tags.append(QString::number(t.id));
            }
        }
    }
    json.insert("tags", tags);
    //Views
    json.insert("views_mode", this->ui.viewsComboBox->currentText());
    json.insert("views", QString::number(this->ui.viewsSpinBox->value()));
    //Watched
    QJsonArray watched = {};
    if (this->ui.watched_yes->isChecked() && this->ui.watched_no->isChecked()) {
        watched = { "All" };
    }
    else if (this->ui.watched_yes->isChecked()) {
        watched = { "Yes" };
    }
    else if (this->ui.watched_no->isChecked()) {
        watched = { "No" };
    }
    json.insert("watched", watched);
    //Rating
    json.insert("rating_mode", this->ui.ratingComboBox->currentText());
    json.insert("rating", QString::number(this->ui.ratingSpinBox->value()));
    json.insert("ignore_defaults", utils::bool_to_text_qt(this->ui.ignore_defaults->isChecked()));
    json.insert("include_unrated", utils::bool_to_text_qt(this->ui.include_unrated->isChecked()));
    return json;
}

bool FilterDialog::eventFilter(QObject* o, QEvent* e)
{
    const QWidget* widget = static_cast<QWidget*>(o);
    if (e->type() == QEvent::Wheel && widget && !widget->hasFocus())
    {
        e->ignore();
        return true;
    }

    return QObject::eventFilter(o, e);
}


FilterDialog::~FilterDialog() {

}