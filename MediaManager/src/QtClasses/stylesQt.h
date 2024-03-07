#pragma once
#include <QPalette>
#include <QBrush>

QPalette get_palette(std::string palette);
QString get_stylesheet(QString stylesheet);
QBrush get_highlight_brush(QPalette current_palette);
QBrush get_highlighted_text_brush(QPalette current_palette);