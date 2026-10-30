#pragma once
#include "colorPaletteExtractor.h"
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

// One resolved mascot, in the shape a QML slot consumes it: which provider id to point the
// Image at, how large the media is natively, where its visual content centre sits and which
// palette dominates it. MascotPrefetcher builds these off the GUI thread.
//
// `contentCenterX` carries the NaN that mascotMediaUtils::contentCenterX() returns when the
// image has no content above the alpha threshold, but no consumer may test it with isnan:
// std::isnan can fold under /fp:fast, and a NaN reaching QML geometry is a silently broken
// slot. Read `hasContentCenter` first and the value only when it is true.
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

// Lets MascotMedia cross queued connections, which is how mediaReady reaches consumers.
Q_DECLARE_METATYPE(MascotMedia)
