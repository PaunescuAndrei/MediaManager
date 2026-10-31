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
// (M4 Task 3 fix: `alpha` is a parameter because the two palettes do not paint the same surface.
// The view's palette paints the row fills, which must be translucent. The viewport's palette paints
// the backdrop *underneath* those rows, so any alpha there is a second scrim: the row fill then
// composites over already-scrimmed media, 216/255 twice, leaving ~2% of the frame -- measured as a
// completely invisible layer. The viewport is therefore given alpha 0, so the media reaches the row
// fill exactly once. This is not a second row alpha: the one-alpha-for-both-ROW-colours rule of
// 7.7.1 is still the single value in BACKGROUND_ROW_ALPHA, and both of the row colours keep it.)
QPalette get_background_row_palette(const QPalette& appPalette, int alpha);