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

QPalette get_background_row_palette(const QPalette& appPalette, int alpha, int alternateAlpha) {
    QPalette rowPalette = appPalette;
    // Base and AlternateBase only, and the same alpha for both: the row colours' hue and their
    // 18/255 separation are what make the alternation readable, so neither is allowed to move.
    // (M4 Task 3 fix: the alpha is the caller's now, see the header. The rows pass
    // BACKGROUND_ROW_ALPHA; the viewport passes 0 and stops being a second scrim between the media
    // and the row fill.)
    // (M5 Task 8d: and the alternate row's alpha is the caller's too -- it is the row colour that does
    // not match the window's background, so it gets its own control. The hues still do not move.)
    for (QPalette::ColorRole role : { QPalette::Base, QPalette::AlternateBase }) {
        QColor colour = rowPalette.color(role);
        colour.setAlpha(role == QPalette::Base ? alpha : alternateAlpha);
        rowPalette.setColor(role, colour);
    }
    return rowPalette;
}

// (M6: get_background_veil_palette() lived here and has no caller left -- the veil is a rectangle in the
// controller's scene, painted from `QPalette::Window` directly, so no palette object is built for it. The
// function is deleted rather than kept "for later": a second way to derive the same colour is exactly what
// the M5 milestone spent a day proving must not exist. `git log -S get_background_veil_palette` has it.)

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