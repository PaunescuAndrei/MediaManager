#include "stdafx.h"
#include "MascotBand.h"
#include <QTimer>
#include <utility>

MascotBand::MascotBand(int slotIndex, QObject* parent)
	: QObject(parent), m_slotIndex(slotIndex)
{
}

MascotBand::~MascotBand()
{
	// A band destroyed while it still owns players would leave them drawing into sinks that go
	// with the QML delegate: destroyVideo() is the one path that detaches them first.
	this->destroyVideo();
}

void MascotBand::setSurface(MascotSurface* surface)
{
	if (this->m_surface == surface)
		return;
	this->m_surface = surface;
	// The recorded slot state described the surface that is going away, not the new one. A
	// rebuilt surface starts at MascotSurface's defaults (FillMode::Fit, video outputs hidden),
	// and a surviving "the slot already carries HeightFit" would suppress the write in
	// applyDisplay() forever after -- the M1 image-layout regression releaseSurface() guards.
	this->m_fillMode = MascotSurface::FillMode::Fit;
	this->m_fillModeKnown = false;
	this->m_videoOutputVisible = false;
	this->m_scrollOffset = 0.0;
}

int MascotBand::slotIndex() const
{
	return this->m_slotIndex;
}

MascotSurface* MascotBand::surface() const
{
	return this->m_surface.data();
}

void MascotBand::setRect(const QRectF& rect)
{
	// Recorded before the surface is asked to take it (M3 Task 4): the value is the band's own
	// geometry, so it answers even while there is no surface to write into, and a surface that has
	// gone cannot be the reason a later travel question has no answer.
	this->m_rect = rect;
	if (!this->m_surface)
		return;
	this->m_surface->setSlotRect(this->m_slotIndex, rect);
}

QRectF MascotBand::rect() const
{
	return this->m_rect;
}

MascotVideoSlot* MascotBand::video() const
{
	return this->m_video.data();
}

bool MascotBand::ensureVideo(int ab)
{
	if (!this->m_video)
		this->m_video = new MascotVideoSlot(this);
	if (!this->m_surface)
		return false;
	// The sinks belong to the QML delegate, so they do not exist until it does. Reading them here
	// and binding them in the same call is what lets the owner start a load without ever holding
	// a sink pointer across a slot count change.
	if (!this->m_surface->slotSink(this->m_slotIndex, ab))
		return false;
	this->m_video->setSinks(this->m_surface->slotSink(this->m_slotIndex, 0),
		this->m_surface->slotSink(this->m_slotIndex, 1));
	return true;
}

void MascotBand::destroyVideo()
{
	if (!this->m_video)
		return;
	// Synchronous, like the surface: the players are detached from the sinks they draw into
	// (which still exist at this point) and then deleted.
	this->m_video->shutdown();
	delete this->m_video.data();
	this->m_video = nullptr;
}

void MascotBand::setMedia(const MascotMedia& media)
{
	this->m_media = media;
	if (!this->m_surface)
		return;
	this->m_surface->setSlotMedia(this->m_slotIndex, media);
}

const MascotMedia& MascotBand::media() const
{
	return this->m_media;
}

void MascotBand::recordMedia(const MascotMedia& media)
{
	this->m_media = media;
}

void MascotBand::forgetMedia()
{
	this->m_media = MascotMedia();
}

void MascotBand::applyMirror(bool mirrored)
{
	if (!this->m_surface)
		return;
	this->m_surface->setSlotMirrored(this->m_slotIndex, mirrored);
}

void MascotBand::applyFill(MascotSurface::FillMode mode)
{
	if (!this->m_surface)
		return;
	if (!this->m_fillModeKnown || this->m_fillMode != mode) {
		this->m_surface->setSlotFillMode(this->m_slotIndex, mode);
		this->m_fillMode = mode;
		this->m_fillModeKnown = true;
	}
}

void MascotBand::setContentOffset(const QPointF& offset)
{
	if (!this->m_surface)
		return;
	this->m_surface->setSlotContentOffset(this->m_slotIndex, offset);
}

void MascotBand::setVideoOpacity(int ab, double opacity, int crossfadeMs)
{
	if (!this->m_surface)
		return;
	this->m_surface->setSlotVideoOpacity(this->m_slotIndex, ab, opacity, crossfadeMs);
}

void MascotBand::setVideoOutputVisible(bool visible)
{
	if (this->m_surface)
		this->m_surface->setSlotVideoVisible(this->m_slotIndex, visible);
	this->m_videoOutputVisible = visible;
}

bool MascotBand::videoOutputVisible() const
{
	return this->m_videoOutputVisible;
}

void MascotBand::clear()
{
	// The order the panel emptied in before the band owned it: the outputs go first, then the
	// slot. `clearSlot()` does not hide them (M2 Task 1's interface fact), so both are needed.
	if (this->m_videoOutputVisible)
		this->setVideoOutputVisible(false);
	if (this->m_surface)
		this->m_surface->clearSlot(this->m_slotIndex);
	this->m_media = MascotMedia();
	this->m_requestPath.clear();
	this->m_requestIsVideo = false;
	this->m_wantedOffsetMs = -1;
	// (M3 Task 4: a queued wrap-point commit goes with the content it was going to replace. The
	// band has just been emptied, so a change parked against the picture that is no longer there
	// must not fire at the first seam after the panel is enabled again.)
	this->m_wrapCommit = WrapCommit();
	// (M3 Task 6A: the band's own cadence and content pipeline go with the content they were
	// serving. A timer left armed here would advance a band this panel no longer fills, and a
	// candidate left held would keep a pin on pixels nothing will show.)
	if (this->m_clipTimer)
		this->m_clipTimer->stop();
	this->m_playback = Playback();
}

QString MascotBand::requestPath() const
{
	return this->m_requestPath;
}

void MascotBand::setRequestPath(const QString& path)
{
	this->m_requestPath = path;
}

bool MascotBand::requestIsVideo() const
{
	return this->m_requestIsVideo;
}

void MascotBand::setRequestIsVideo(bool video)
{
	this->m_requestIsVideo = video;
}

qint64 MascotBand::wantedOffsetMs() const
{
	return this->m_wantedOffsetMs;
}

void MascotBand::setWantedOffsetMs(qint64 offsetMs)
{
	this->m_wantedOffsetMs = offsetMs;
}

MascotBand::Playback& MascotBand::playback()
{
	return this->m_playback;
}

const MascotBand::Playback& MascotBand::playback() const
{
	return this->m_playback;
}

QTimer* MascotBand::clipTimer() const
{
	return this->m_clipTimer;
}

QTimer* MascotBand::ensureClipTimer()
{
	if (!this->m_clipTimer) {
		// Single-shot, like the panel timer it replaces: every tick is answered by a fresh commit
		// that re-arms it. A child of the band, so the band's end stops it even if the controller
		// never does.
		this->m_clipTimer = new QTimer(this);
		this->m_clipTimer->setSingleShot(true);
	}
	return this->m_clipTimer;
}

double MascotBand::scrollOffset() const
{
	return this->m_scrollOffset;
}

void MascotBand::setScrollOffset(double offset)
{
	this->m_scrollOffset = offset;
}

void MascotBand::queueWrapCommit(WrapCommit commit)
{
	// Replaced, not appended: a band has one seam at a time and one queued change, so a second
	// request for the same pass supersedes the first instead of the band committing two content
	// changes at one wrap (Task 6 queues at most one per pass anyway).
	this->m_wrapCommit = std::move(commit);
}

bool MascotBand::hasWrapCommit() const
{
	return static_cast<bool>(this->m_wrapCommit);
}

MascotBand::WrapCommit MascotBand::takeWrapCommit()
{
	WrapCommit commit = std::move(this->m_wrapCommit);
	this->m_wrapCommit = WrapCommit();
	return commit;
}
