#pragma once
#include "MascotMedia.h"
#include "MascotSurface.h"
#include "MascotVideoSlot.h"
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <functional>

// One band of one mascot panel: the slot it draws into, the media that slot carries, the players
// that play a video into it, and the request bookkeeping that says what it is waiting for.
//
// MascotController keeps the policy -- the configuration, the mode and the band counts, the
// picks, the cache pins, the mirror rule and the cadence -- and this class keeps the rendering
// state that policy produces. Every write into the slot goes through here, so a band can be
// re-applied without its owner repeating the geometry, the mirror or the media.
//
// Like MascotSurface and MascotVideoSlot this is GUI thread only: the players, the slot's QML
// delegate and the sinks all live there.
class MascotBand : public QObject {
	Q_OBJECT
public:
	// `slotIndex` is fixed for the band's life: it is the slot inside the surface this band draws
	// into, and the index the surface's QML delegate is found by (amendment O).
	explicit MascotBand(int slotIndex, QObject* parent = nullptr);
	~MascotBand() override;

	// The surface hosting this band; null on teardown. A band outlives the surface it was
	// configured for, so a panel rebuilt (or a mode changed, in a later task) does not lose the
	// band object -- the recorded slot state is reset instead, because it described the surface
	// that has just gone.
	void setSurface(MascotSurface* surface);
	int slotIndex() const;
	// The surface this band draws into; null on teardown. Read-only and non-owning, like video():
	// a caller that needs the surface's own geometry (M3 Task 4's scroll travel asks it for the
	// size the media is painted at) must not hold it across a call, and one that has gone is a
	// band that cannot draw, let alone pan.
	MascotSurface* surface() const;

	// The band's rectangle inside the surface. The owner chooses the geometry; writing it here is
	// what keeps every surface write in one place.
	void setRect(const QRectF& rect);
	// The rectangle setRect() last recorded. Recorded here rather than read back off the surface
	// because the geometry is the band's own value: the scroll driver derives its travel from this
	// rectangle and the painted content size, and it needs an answer even while the surface holds
	// no slot state of its own (a delegate QML has not created yet).
	QRectF rect() const;

	// The band's players, created on first use. ensureVideo() creates the MascotVideoSlot and
	// binds its two sinks when the delegate that owns them exists; false means the sink for `ab`
	// is not there yet, so the caller retries. The slot itself is created either way, because a
	// panel's players are its own and are configured before the first load.
	MascotVideoSlot* video() const;
	bool ensureVideo(int ab);
	// shutdown() then delete, in that order: the players draw into sinks that belong to the QML
	// delegate, so they are detached before the object goes.
	void destroyVideo();

	// Records `media` and writes it into the slot. The owner applies the mirror and the fill mode
	// first, because the media write re-derives the content shift from both -- the order the
	// retired applyDisplay() used, kept so the slot never paints one apply with the new media
	// under the old mirror.
	void setMedia(const MascotMedia& media);
	const MascotMedia& media() const;
	// Records `media` without writing the slot. One caller: showMedia() records it before the
	// cache pins are re-evaluated -- amendment H2's invariant needs the new path to be the shown
	// one when pinReferenced() runs, and the write has to wait for the mirror and the fill mode.
	void recordMedia(const MascotMedia& media);
	// Drops the recorded media without touching the slot. The one caller is the decoder clamp,
	// where the panel goes on drawing the last video frame until an image lands, so clearing the
	// slot there would blank it.
	void forgetMedia();

	void applyMirror(bool mirrored);
	// Writes the mode only when it differs from the one the slot already carries. The recorded
	// value and the "one has been written" flag are what make an image commit write no fill mode
	// at all (M2), so the image path costs exactly what it cost in M1.
	void applyFill(MascotSurface::FillMode mode);
	void setContentOffset(const QPointF& offset);
	void setVideoOpacity(int ab, double opacity, int crossfadeMs);
	// Writes the slot's video visibility and records it, so the owner can ask whether the outputs
	// it is about to hide are showing anything.
	void setVideoOutputVisible(bool visible);
	bool videoOutputVisible() const;

	// Forgets the media and the request and empties the slot: clearSlot() plus hiding the video
	// outputs, in the order the panel used to empty them. The players are left alone -- whether
	// they live is the owner's decision (a hidden panel keeps them, a released surface does not).
	void clear();

	// The path this band is waiting for. A media is applied only while it is what the band asked
	// for, so a late delivery can never overwrite a newer pick.
	QString requestPath() const;
	void setRequestPath(const QString& path);
	// True while requestPath() names a video a player is loading rather than an image the
	// prefetcher is decoding.
	bool requestIsVideo() const;
	void setRequestIsVideo(bool video);
	// The offset this band asked its player for; -1 is the configured random start.
	qint64 wantedOffsetMs() const;
	void setWantedOffsetMs(qint64 offsetMs);

	// The offset the band last wrote for the scroll driver, so a tick writes only on change. (M3
	// Task 4 owns the write into the surface; this is its record until then.)
	// (M3 Task 4: the driver is that write now -- it calls the surface's one-write setter and then
	// records the value here, which is what makes an unchanged tick cost no property write at
	// all. The record is also what a band that stops panning compares against when it has to be
	// re-centred.)
	double scrollOffset() const;
	void setScrollOffset(double offset);

	// M3 Task 4: the wrap-point commit. A caller that wants this band's content changed while it
	// is panning queues the change here instead of applying it mid-pass; the scroll driver runs it
	// once, at that band's next seam -- the point where its pan wraps, or where a pingpong turns,
	// which is the one moment a hard reposition already moves the picture. Only the mechanism
	// lives here: the cadence that decides *when* a change is queued is the caller's (Task 6).
	//
	// A change queued while the band cannot pan (travel 0) never runs, because such a band has no
	// seam; a change queued on a band that is destroyed with it is dropped with the band. Both are
	// the caller's problem to avoid, and both are stated here rather than silently handled.
	using WrapCommit = std::function<void()>;
	void queueWrapCommit(WrapCommit commit);
	bool hasWrapCommit() const;
	// Takes the queued change out, leaving nothing queued, so one seam can never run it twice.
	WrapCommit takeWrapCommit();

private:
	int m_slotIndex = 0;
	// QPointer for the same reason every surface reference in this module is one: the surface can
	// be destroyed by its host going away, and a null pointer is answerable while a dangling one
	// is not.
	QPointer<MascotSurface> m_surface;
	// What the panel displays, including the palette and content centre it came with.
	MascotMedia m_media;
	// The path this panel is waiting for. A media is applied to a panel only while it is
	// what that panel asked for, so a late delivery can never overwrite a newer pick.
	QString m_requestPath;
	// True while requestedPath names a video a player is loading rather than an image the
	// prefetcher is decoding. Both waits share the one field because both mean "this panel is
	// waiting for exactly that path", but only the delivery that matches may be applied.
	bool m_requestIsVideo = false;
	// The panel's players: two outputs that take turns across a crossfade, a gate that only
	// reports a clip once a frame of it is on screen, and a deadline. QPointer for the same
	// reason the surface uses one: it is destroyed in releaseSurface() *because* it draws into
	// sinks the surface owns, and a null pointer is answerable while a dangling one is not.
	// (M3 Task 1: the destruction is destroyVideo() on this band now; releaseSurface() still
	// calls it before the surface goes, which is what keeps the order the sentence describes.)
	QPointer<MascotVideoSlot> m_video;
	// The offset this panel asked its player for: -1 is the configured random start, and a
	// mirror copy supplies the left panel's exact offset instead. Recorded so the log says
	// which of the two a swap asked for, and so the mirror copy cannot invent its own.
	qint64 m_wantedOffsetMs = -1;
	// The fill mode this panel's slot currently carries, and whether one has been written at
	// all. M2 moved that write out of configureSurface() into applyDisplay(), where the mode
	// follows the media type; keeping the last value here is what makes an image commit write
	// no fill mode at all, so the image path costs exactly what it cost in M1.
	MascotSurface::FillMode m_fillMode = MascotSurface::FillMode::HeightFit;
	bool m_fillModeKnown = false;
	// Whether the slot's video outputs are currently shown. `clearSlot()` does not hide them
	// (Task 1's interface fact), so the flag both makes the hiding the controller's job and
	// lets the image path leave the outputs alone entirely while it has never shown one.
	// (M3 Task 1: the flag and the hiding are the band's now; the owner still decides when, and
	// the reason for keeping the flag is unchanged.)
	bool m_videoOutputVisible = false;
	double m_scrollOffset = 0.0;
	// The rectangle the owner last gave this band (M3 Task 4). Kept because the scroll driver
	// needs the band's own extent as a value, not as a question the surface may have no answer to.
	QRectF m_rect;
	// The change a seam will commit (M3 Task 4). Empty when nothing is queued, which is the state
	// a tick reads on every band and therefore the state that must cost nothing.
	WrapCommit m_wrapCommit;
};
