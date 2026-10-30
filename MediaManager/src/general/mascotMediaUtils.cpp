#include "stdafx.h"
#include "mascotMediaUtils.h"
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

bool mascotMediaUtils::findAlphaBoundsX(const QImage& image, int alphaThreshold, int* minX, int* maxX) {
	if (image.isNull())
		return false;
	int min_bound = image.width();
	int max_bound = -1;
	for (int y = 0; y < image.height(); ++y) {
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			if (qAlpha(line[x]) > alphaThreshold) {
				if (x < min_bound)
					min_bound = x;
				if (x > max_bound)
					max_bound = x;
			}
		}
	}
	if (max_bound < 0)
		return false;
	*minX = min_bound;
	*maxX = max_bound;
	return true;
}

bool mascotMediaUtils::computeWeightedCenterXFromAlpha(const QImage& image, int alphaThreshold, double* centerX) {
	if (image.isNull())
		return false;
	const int width = image.width();
	const int height = image.height();
	if (width <= 0 || height <= 0)
		return false;

	const int inf = width + height + 10;
	const int size = width * height;
	QVector<int> dist(size, inf);

	auto index = [width](int x, int y) { return y * width + x; };

	for (int y = 0; y < height; ++y) {
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < width; ++x) {
			if (qAlpha(line[x]) <= alphaThreshold)
				dist[index(x, y)] = 0;
		}
	}

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int& d = dist[index(x, y)];
			if (d == 0)
				continue;
			if (x > 0)
				d = std::min(d, dist[index(x - 1, y)] + 1);
			if (y > 0)
				d = std::min(d, dist[index(x, y - 1)] + 1);
			if (x > 0 && y > 0)
				d = std::min(d, dist[index(x - 1, y - 1)] + 2);
			if (x + 1 < width && y > 0)
				d = std::min(d, dist[index(x + 1, y - 1)] + 2);
		}
	}

	for (int y = height - 1; y >= 0; --y) {
		for (int x = width - 1; x >= 0; --x) {
			int& d = dist[index(x, y)];
			if (d == 0)
				continue;
			if (x + 1 < width)
				d = std::min(d, dist[index(x + 1, y)] + 1);
			if (y + 1 < height)
				d = std::min(d, dist[index(x, y + 1)] + 1);
			if (x + 1 < width && y + 1 < height)
				d = std::min(d, dist[index(x + 1, y + 1)] + 2);
			if (x > 0 && y + 1 < height)
				d = std::min(d, dist[index(x - 1, y + 1)] + 2);
		}
	}

	double sumW = 0.0;
	double sumX = 0.0;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int d = dist[index(x, y)];
			if (d > 0 && d < inf) {
				double weight = static_cast<double>(d);
				sumW += weight;
				sumX += weight * static_cast<double>(x);
			}
		}
	}

	if (sumW <= 0.0)
		return false;
	*centerX = sumX / sumW;
	return true;
}

double mascotMediaUtils::contentCenterX(const QImage& image, int alphaThreshold) {
	double centerX = 0.0;
	if (computeWeightedCenterXFromAlpha(image, alphaThreshold, &centerX))
		return centerX;
	int minX = 0;
	int maxX = 0;
	if (findAlphaBoundsX(image, alphaThreshold, &minX, &maxX))
		return (static_cast<double>(minX) + static_cast<double>(maxX)) / 2.0;
	return std::numeric_limits<double>::quiet_NaN();
}
