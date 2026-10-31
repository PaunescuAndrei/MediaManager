#pragma once
#include <QPalette>
#include <QBrush>

// Alpha the list's row fills carry while the background layer is on. One value for both row
// colours, deliberately: the alternation survives only because Base and AlternateBase are
// composited with the same alpha, so the two rows keep the same distance between them over
// whatever the layer puts behind them. Two different alphas would pull one band towards the
// video more than the other and flatten the alternation into a single shade.
constexpr int BACKGROUND_ROW_ALPHA = 216;

QPalette get_palette(std::string palette);
QString get_stylesheet(QString stylesheet);
QBrush get_highlight_brush(QPalette current_palette);
QBrush get_highlighted_text_brush(QPalette current_palette);
// `appPalette` with Base and AlternateBase made translucent, everything else untouched.
// Every other role -- Highlight in particular -- stays exactly as it was, so selection, text,
// the header and the scrollbars keep painting opaque. 216/255 is measured, not taste: the
// composited row colour still leaves white body text at 9.6:1 (Base) and 7.6:1 (AlternateBase)
// against WCAG's 4.5:1 floor even over a pure-white frame at full background opacity, while the
// media underneath reads as a tint rather than a wash.
QPalette get_background_row_palette(const QPalette& appPalette);