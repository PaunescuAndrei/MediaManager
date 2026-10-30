#pragma once
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSet>
#include <QString>

class MascotImageCache;

// Serves image://mascots/<id> requests out of the decoded-mascot cache (Task 2).
//
// The engine owns this provider (QQmlEngine::addImageProvider) and calls requestImage() from
// whichever thread loads the pixels for an Image item. This provider sets no
// ForceAsynchronousImageLoading flag and MascotSlot.qml pins Image.asynchronous to false, so in
// practice that is the GUI thread, during the slot's property write; the cache's own mutex still
// covers the case where a future slot asks from another thread. Nothing here ever waits: a miss
// returns a null image instead of blocking until a decode arrives, so the worst thing an
// unservable id can do is leave its slot blank for a frame. MascotPrefetcher inserts into the
// cache before it announces a media, so a slot that reacts to mediaReady always finds its pixels.
class MascotImageProvider : public QQuickImageProvider {
	Q_OBJECT
public:
	// `cache` is not owned and must outlive the engine that owns this provider.
	explicit MascotImageProvider(MascotImageCache* cache);

	// `id` is the percent-encoded cache key MascotPrefetcher built, possibly already decoded
	// by Qt: both forms are tried (amendment M). `requestedSize` is ignored on purpose -- the
	// cache already holds the pixels at native size, and scaling them here would copy the
	// whole image on the render thread for every request; Qt scales the texture on the GPU.
	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	MascotImageCache* cache = nullptr;
	// A miss is reported once per id: an Image item asks again whenever its source is
	// re-applied, and an id that was evicted would otherwise log on every frame. requestImage()
	// is not promised to run on a single thread, so the set has its own mutex.
	QMutex missLogMutex;
	QSet<QString> loggedMissIds;
};
