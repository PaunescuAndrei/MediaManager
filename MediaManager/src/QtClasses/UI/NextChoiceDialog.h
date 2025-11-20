#pragma once
#include <QDialog>
#include "ui_NextChoiceDialog.h"
#include "definitions.h"

class NextChoiceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NextChoiceDialog(QWidget* parent = nullptr);
    ~NextChoiceDialog() = default;
    void setChoices(const QList<NextVideoChoice>& newChoices);
    NextVideoChoice selectedChoice() const;
private:
    void updateSelectionState();
    QList<NextVideoChoice> choices;
    Ui::NextChoiceDialog ui;
};
