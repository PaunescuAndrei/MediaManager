#include "stdafx.h"
#include "stylesQt.h"
#include <QColor>

QPalette get_palette(std::string palette) {
    QPalette paletteQt = QPalette();
    if (palette == "main") {
        paletteQt.setColor(QPalette::Window, QColor(53, 53, 53));
        paletteQt.setColor(QPalette::WindowText, QColorConstants::White);
        paletteQt.setColor(QPalette::Base, QColor(35, 35, 35));
        paletteQt.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        paletteQt.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
        paletteQt.setColor(QPalette::ToolTipText, QColorConstants::White);
        paletteQt.setColor(QPalette::Text, QColorConstants::White);
        paletteQt.setColor(QPalette::Button, QColor(53, 53, 53));
        paletteQt.setColor(QPalette::ButtonText, QColorConstants::White);
        paletteQt.setColor(QPalette::BrightText, QColorConstants::Red);
        paletteQt.setColor(QPalette::Link, QColor(42, 130, 218));
        paletteQt.setColor(QPalette::Highlight, QColor(242, 112, 242));
        paletteQt.setColor(QPalette::HighlightedText, QColor(35, 35, 35));
        paletteQt.setColor(QPalette::Active, QPalette::Button, QColor(53, 53, 53));
        paletteQt.setColor(QPalette::Disabled, QPalette::ButtonText, QColorConstants::DarkGray);
        paletteQt.setColor(QPalette::Disabled, QPalette::WindowText, QColorConstants::DarkGray);
        paletteQt.setColor(QPalette::Disabled, QPalette::Text, QColorConstants::DarkGray);
        paletteQt.setColor(QPalette::Disabled, QPalette::Light, QColor(53, 53, 53));
    }
    else if (palette == "spinbox") {
        paletteQt.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
        paletteQt.setColor(QPalette::HighlightedText, QColorConstants::White);
    }
    return paletteQt;
}

QString get_stylesheet(QString stylesheet) {
    QString ss = QString();
    if (stylesheet == "spinbox") {
        ss = QStringLiteral("QSpinBox { selection-background-color: transparent; selection-color: palette(Text);}");
    }
    if (stylesheet == "doublespinbox") {
        ss = QStringLiteral("QDoubleSpinBox { selection-background-color: transparent; selection-color: palette(Text);}");
    }
    if (stylesheet == "videoswidget") {
        ss = QStringLiteral("QToolTip { color: palette(Text); background-color: palette(Base); border: 0px;}");
    }
    return ss;
}

QBrush get_highlight_brush(QPalette current_palette) {
    return QBrush(current_palette.highlight().color().darker(115));
}

QBrush get_highlighted_text_brush(QPalette current_palette) {
    return current_palette.highlightedText();
}