#pragma once
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSet>
#include <QString>

class MascotImageCache;

class MascotImageProvider : public QQuickImageProvider {
	Q_OBJECT
public:
	// `cache` is not owned and must outlive the engine that owns this provider.
	explicit MascotImageProvider(MascotImageCache* cache);

	// `id` arrives in whatever form Qt passes it -- raw cache key, still percent-encoded, or
	// needing re-encoding; all three are tried. `requestedSize` is ignored on purpose: Qt scales
	// the texture on the GPU, while scaling here would copy the image on the render thread per request.
	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	MascotImageCache* cache = nullptr;
	// requestImage() is not promised to run on one thread (in practice the GUI thread, because
	// MascotSlot.qml pins Image.asynchronous false), so the set has its own mutex.
	QMutex missLogMutex;
	QSet<QString> loggedMissIds;
};
