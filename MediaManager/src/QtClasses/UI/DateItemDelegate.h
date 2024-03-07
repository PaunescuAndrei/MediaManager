#pragma once
#include <QStyledItemDelegate>

class DateItemDelegate :
    public QStyledItemDelegate
{
public:
    QString format;
    DateItemDelegate(QObject* parent,QString format = "") :
        QStyledItemDelegate(parent) {
        this->format = format;
    };

    QString displayText(const QVariant& value, const QLocale& locale) const
    {
        if (value.type() == QVariant::DateTime)
        {
            if(!this->format.isEmpty())
                return value.toDateTime().toString(this->format);
        }
        return QStyledItemDelegate::displayText(value, locale);
    }
};
