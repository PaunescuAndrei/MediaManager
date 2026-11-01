#pragma once
#include "colorPaletteExtractor.h"
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

// `contentCenterX` can be NaN. Test `hasContentCenter`, never isnan: std::isnan folds under
// /fp:fast, and a NaN reaching QML geometry silently breaks the slot.
struct MascotMedia {
	enum Type { Image, Video };

	Type type = Image;
	QString path;
	QString providerId;
	QSize nativeSize;
	bool hasContentCenter = false;
	double contentCenterX = 0.0;
	QList<color_area> acceptedColors;
	QList<color_area> rejectedColors;
	qint64 startOffsetMs = 0;
};

// Required for the queued connection that carries mediaReady: MascotPrefetcher builds these off the
// GUI thread.
Q_DECLARE_METATYPE(MascotMedia)
