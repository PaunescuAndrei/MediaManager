#include "stdafx.h"
#include "MascotBand.h"

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

void MascotBand::setRect(const QRectF& rect)
{
	if (!this->m_surface)
		return;
	this->m_surface->setSlotRect(this->m_slotIndex, rect);
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

double MascotBand::scrollOffset() const
{
	return this->m_scrollOffset;
}

void MascotBand::setScrollOffset(double offset)
{
	this->m_scrollOffset = offset;
}
