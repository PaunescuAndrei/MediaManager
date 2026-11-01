#pragma once
#include <QImage>

namespace mascotMediaUtils {
	bool findAlphaBoundsX(const QImage& image, int alphaThreshold, int* minX, int* maxX);
	bool computeWeightedCenterXFromAlpha(const QImage& image, int alphaThreshold, double* centerX);
	// `alphaThreshold` defaults to 77 (30% alpha); answers NaN when no pixel clears it.
	double contentCenterX(const QImage& image, int alphaThreshold = 77);
}
