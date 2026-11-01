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

class QTimer;

// GUI thread only: the players, the slot's QML delegate and the sinks all live there.
class MascotBand : public QObject {
	Q_OBJECT
public:
	// `slotIndex` is fixed for the band's life; it is the slot the surface's QML delegate is found by.
	explicit MascotBand(int slotIndex, QObject* parent = nullptr);
	~MascotBand() override;

	// A band outlives the surface it was configured for; the recorded slot state is reset because it described the surface just gone.
	void setSurface(MascotSurface* surface);
	int slotIndex() const;
	void setGroup(MascotSurface::SlotGroup group);
	MascotSurface* surface() const;

	void setRect(const QRectF& rect);
	QRectF rect() const;

	MascotVideoSlot* video() const;
	bool ensureVideo(int ab);
	// shutdown() then delete: the players draw into sinks the delegate owns, so they are detached before the object goes.
	void destroyVideo();

	// The owner writes the mirror and the fill mode first; the media write re-derives the content shift from both.
	void setMedia(const MascotMedia& media);
	const MascotMedia& media() const;
	void recordMedia(const MascotMedia& media);
	// Drops the media without touching the slot: the decoder clamp keeps drawing the last frame until an image lands.
	void forgetMedia();

	void applyMirror(bool mirrored);
	// Writes only when the mode differs, so an image commit writes no fill mode at all.
	void applyFill(MascotSurface::FillMode mode);
	void setContentOffset(const QPointF& offset);
	void setVideoOpacity(int ab, double opacity, int crossfadeMs);
	void setVideoOutputVisible(bool visible);
	bool videoOutputVisible() const;

	// The players are left alone: whether they live is the owner's decision.
	void clear();

	QString requestPath() const;
	void setRequestPath(const QString& path);
	bool requestIsVideo() const;
	void setRequestIsVideo(bool video);
	qint64 wantedOffsetMs() const;
	void setWantedOffsetMs(qint64 offsetMs);

	struct Playback {
		QString prefetchPath;
		MascotMedia candidate;
		// Set on the right band while the left band's mirrored pick is still loading; the copy starts from that pick's firstFrame(), where its offset exists.
		bool mirrorCopyPending = false;
		quint64 videoSeq = 0;
		qint64 requestedAtMs = 0;
		qint64 committedAtMs = 0;
		int videoFailures = 0;
		int imageFailures = 0;
		int videoSinkRetries = 0;
		bool videoPickRetryPending = false;
		int videoLoadRetries = 0;
		QString lastFailedVideoPath;
		qint64 lastFailedVideoLogMs = 0;
	};
	Playback& playback();
	const Playback& playback() const;

	QTimer* clipTimer() const;
	QTimer* ensureClipTimer();

	double scrollOffset() const;
	void setScrollOffset(double offset);

	// Runs once at the band's next seam, the one moment a reposition already moves the picture; a band that cannot pan never runs it.
	using WrapCommit = std::function<void()>;
	void queueWrapCommit(WrapCommit commit);
	bool hasWrapCommit() const;
	WrapCommit takeWrapCommit();
	void discardWrapCommit();

private:
	int m_slotIndex = 0;
	QPointer<MascotSurface> m_surface;
	MascotMedia m_media;
	QString m_requestPath;
	bool m_requestIsVideo = false;
	QPointer<MascotVideoSlot> m_video;
	qint64 m_wantedOffsetMs = -1;
	MascotSurface::FillMode m_fillMode = MascotSurface::FillMode::HeightFit;
	bool m_fillModeKnown = false;
	bool m_videoOutputVisible = false;
	double m_scrollOffset = 0.0;
	QRectF m_rect;
	WrapCommit m_wrapCommit;
	Playback m_playback;
	QTimer* m_clipTimer = nullptr;
};
