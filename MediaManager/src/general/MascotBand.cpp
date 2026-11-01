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
	// destroyVideo() is the one path that detaches players from sinks that go with the QML delegate before deleting them.
	this->destroyVideo();
}

void MascotBand::setSurface(MascotSurface* surface)
{
	if (this->m_surface == surface)
		return;
	this->m_surface = surface;
	// The recorded slot state described the surface that is going away: a rebuilt surface starts at MascotSurface's
	// defaults, and a surviving "the slot already carries HeightFit" would suppress that write forever after.
	this->m_fillMode = MascotSurface::FillMode::Fit;
	this->m_fillModeKnown = false;
	this->m_videoOutputVisible = false;
	this->m_scrollOffset = 0.0;
}

int MascotBand::slotIndex() const
{
	return this->m_slotIndex;
}

void MascotBand::setGroup(MascotSurface::SlotGroup group)
{
	if (!this->m_surface)
		return;
	this->m_surface->setSlotGroup(this->m_slotIndex, group);
}

MascotSurface* MascotBand::surface() const
{
	return this->m_surface.data();
}

void MascotBand::setRect(const QRectF& rect)
{
	// Recorded before the surface is asked: the band's own geometry must answer even while there is no surface to write into.
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
	// The sinks belong to the QML delegate, so reading and binding them in one call is what lets the owner start a load
	// without holding a sink pointer across a slot count change.
	QVideoSink* sinkA = this->m_surface->slotSink(this->m_slotIndex, 0);
	QVideoSink* sinkB = this->m_surface->slotSink(this->m_slotIndex, 1);
	if (!sinkA || (ab != 0 && !sinkB))
		return false;
	this->m_video->setSinks(sinkA, sinkB);
	return true;
}

void MascotBand::destroyVideo()
{
	if (!this->m_video)
		return;
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
	// The outputs go first, then the slot: clearSlot() does not hide them. Both opacities go back to zero as well,
	// because the slot outlives the band and its next owner fades in from its own first frame.
	if (this->m_videoOutputVisible)
		this->setVideoOutputVisible(false);
	this->setVideoOpacity(0, 0.0, 0);
	this->setVideoOpacity(1, 0.0, 0);
	if (this->m_surface)
		this->m_surface->clearSlot(this->m_slotIndex);
	this->m_media = MascotMedia();
	this->m_requestPath.clear();
	this->m_requestIsVideo = false;
	this->m_wantedOffsetMs = -1;
	// A queued wrap-point commit goes with the content it was going to replace.
	this->m_wrapCommit = WrapCommit();
	// The band's cadence and pipeline go with the content: an armed timer would advance a band nothing fills.
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
		// Single-shot, because every tick is answered by a fresh commit; a child of the band, so the band's end stops it.
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
	// Replaced, not appended: a band has one seam at a time, so a second request supersedes the first.
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

void MascotBand::discardWrapCommit()
{
	this->m_wrapCommit = WrapCommit();
}
