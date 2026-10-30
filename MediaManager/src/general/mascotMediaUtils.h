#pragma once
#include <QImage>

// Horizontal centering helpers for mascot content. They live as free functions
// so the QML-backed mascot renderer and the legacy customGraphicsView widget
// share one implementation and therefore produce an identical content center.
namespace mascotMediaUtils {
	// Bounds of the columns that contain pixels with alpha above the threshold.
	bool findAlphaBoundsX(const QImage& image, int alphaThreshold, int* minX, int* maxX);
	// Alpha-weighted horizontal center of mass.
	bool computeWeightedCenterXFromAlpha(const QImage& image, int alphaThreshold, double* centerX);
	// Weighted center with an alpha-bounds fallback; returns NaN when no content is found.
	double contentCenterX(const QImage& image, int alphaThreshold = 77); // 30% alpha
}
