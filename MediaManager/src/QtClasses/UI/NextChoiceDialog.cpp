#include "stdafx.h"
#include "NextChoiceDialog.h"
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QPushButton>

NextChoiceDialog::NextChoiceDialog(QWidget* parent) : QDialog(parent)
{
    this->ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);

    connect(this->ui.buttonBox, &QDialogButtonBox::accepted, this, &NextChoiceDialog::accept);
    connect(this->ui.buttonBox, &QDialogButtonBox::rejected, this, &NextChoiceDialog::reject);
    connect(this->ui.choicesList, &QListWidget::itemSelectionChanged, this, &NextChoiceDialog::updateSelectionState);
    connect(this->ui.choicesList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        this->accept();
    });

    this->updateSelectionState();
}

void NextChoiceDialog::setChoices(const QList<NextVideoChoice>& newChoices)
{
    this->choices = newChoices;
    this->ui.choicesList->clear();

    for (int i = 0; i < this->choices.size(); ++i) {
        const NextVideoChoice& choice = this->choices[i];
        QString title = choice.name.isEmpty() ? QFileInfo(choice.path).completeBaseName() : choice.name;
        QString authorText = choice.author.isEmpty() ? QString() : QString(" - %1").arg(choice.author);
        QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1%2").arg(title, authorText));
        item->setData(Qt::UserRole, i);
        item->setToolTip(choice.path);
        this->ui.choicesList->addItem(item);
    }

    if (!this->choices.isEmpty()) {
        this->ui.choicesList->setCurrentRow(0);
    }
    this->updateSelectionState();
}

NextVideoChoice NextChoiceDialog::selectedChoice() const
{
    int row = this->ui.choicesList->currentRow();
    if (row >= 0 && row < this->choices.size()) {
        return this->choices[row];
    }
    return NextVideoChoice();
}

void NextChoiceDialog::updateSelectionState()
{
    int row = this->ui.choicesList->currentRow();
    QString details = QStringLiteral("Select a video to see its details.");
    if (row >= 0 && row < this->choices.size()) {
        const NextVideoChoice& choice = this->choices[row];
        QString title = choice.name.isEmpty() ? QFileInfo(choice.path).completeBaseName() : choice.name;
        QString authorText = choice.author.isEmpty() ? QString() : QString(" - %1").arg(choice.author);
        details = QStringLiteral("%1%2\n%3").arg(title, authorText, choice.path);
    }
    this->ui.choiceDetailsLabel->setText(details);

    QPushButton* okButton = this->ui.buttonBox->button(QDialogButtonBox::Ok);
    if (okButton) {
        okButton->setEnabled(row >= 0 && row < this->choices.size());
    }
}
