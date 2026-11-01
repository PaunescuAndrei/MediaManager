#pragma once
#include <QPalette>
#include <QBrush>

// Alpha the row fills carry while the background layer is on. 216/255 is measured, not taste: over a
// pure-white frame at full background opacity the composited row colour still leaves white body text
// at 9.6:1 (Base) and 7.6:1 (AlternateBase), against WCAG's 4.5:1 floor.
constexpr int BACKGROUND_ROW_ALPHA = 216;

QPalette get_palette(std::string palette);
QString get_stylesheet(QString stylesheet);
QBrush get_highlight_brush(QPalette current_palette);
QBrush get_highlighted_text_brush(QPalette current_palette);
// `alpha` is a parameter because the two palettes paint different surfaces: the view's paints the row
// fills and must be translucent, the viewport's paints the backdrop underneath them, where any alpha is
// a second scrim -- 216/255 twice leaves ~2% of the frame, so the viewport passes 0. The alternate row,
// the one whose colour does not match the window background, gets its own alpha for text legibility.
QPalette get_background_row_palette(const QPalette& appPalette, int alpha, int alternateAlpha);
// The veil is a second surface in front of the layer, not a replacement for the row fill: it is
// `QPalette::Window` at the configured alpha, behind every pane and in the window's own colour
// rather than the list's Base, so at alpha 255 it is indistinguishable from the window with the
// layer switched off. It is painted from that colour by MascotController::pushVeilState(), so there
// is no palette builder for it.
