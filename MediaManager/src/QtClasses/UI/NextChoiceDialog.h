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
    void rebuildCards();
    QWidget* buildCard(const NextVideoChoice& choice, int index);
    void applySelection(int index);
    QList<NextVideoChoice> choices;
    Ui::NextChoiceDialog ui;
    int currentIndex = -1;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};
