#pragma once
#include "MascotMedia.h"
#include "MascotMediaSource.h"
// Included rather than forward declared: QPointer<T> needs a complete T.
#include "MascotPrefetcher.h"
#include "MascotSurface.h"
#include "MascotVideoSlot.h"
#include "MascotBand.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <Qt>
#include <QVector>
#include <QElapsedTimer>
#include <memory>
#include <vector>

class MainApp;
class MascotImageCache;
class QTimer;

// Pair-only policy (mirroring, flips, clicks, drops, theme colour).
enum class MascotSide { Left, Right };

// Per-panel identity: the only conversion from a side is panelOf(), and there is no inverse because
// the background belongs to no pair.
enum class MascotPanel { Left, Right, Background };

// One band covering the panel for `Static` and `Scroll`; N stripes for `Stripes`.
enum class MascotMode { Static, Scroll, Stripes };

// "Forward" starts at the content's near end; a direction naming the other axis keeps its sense.
enum class ScrollDirection { LeftToRight, RightToLeft, TopToBottom, BottomToTop };

// The panning axis is perpendicular, so orientation, fill mode and scroll axis derive from it.
enum class StripesOrientation { Horizontal, Vertical };

// Decides what each mascot panel shows: which media, when to swap, how it is mirrored. Drives one
// shared MascotSurface scene for all three layers, because a QQuickWidget carrying
// Qt::WA_AlwaysStackOnTop is drawn above every sibling whatever the widget stack says.
// GUI thread only; the prefetcher emits mediaReady/mediaFailed here on the GUI thread.
class MascotController : public QObject {
	Q_OBJECT
public:
	// All pointers must outlive this object; `cache` only builds the surfaces.
	MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
		QWidget* leftHost, QWidget* rightHost, QWidget* backgroundHost, QObject* parent = nullptr);
	~MascotController() override;

	// A user who never enables the renderer pays no Qt Quick cost.
	void start();
	// Drops queued work, unpins everything, clears the slots and deletes the sheet. Idempotent.
	void stop();
	bool isRunning() const;
	// Tears down only the two columns, leaving the background layer running.
	void stopPanels();

	void applyConfig();
	// Each slot is the rectangle of the host the panel's content is laid out in.
	void updateLayout();
	void pushVeil();

	// `ok` is false for anything but the three names; the answer is then `Static`.
	static MascotMode modeFromString(const QString& value, bool* ok);
	// `ok` is false for anything but `Horizontal`/`Vertical`; the answer is then `Horizontal`.
	static StripesOrientation stripesOrientationFromString(const QString& value, bool* ok);

	// The background derives its count from the players it was funded, not from the mode.
	int bandCount(MascotPanel panel) const;
	QVector<QRectF> bandRects(MascotPanel panel) const;
	// 0 means images -- a band is never left blank, it shows a picture.
	int videoBandsFor(MascotPanel panel) const;
	// Creates or destroys bands from the tail; a surface is resized in place, never replaced.
	void applyMode();

	// One QTimer and one QElapsedTimer for both panels and every band; the timer exists only while
	// a panning mode is active and the panels can be seen, so `Static` allocates nothing.
	//
	// The phase is a fraction of a pass, wrapped with fmod, so a resize rescales the pan and a stall
	// moves a band by at most one travel.
	void startScrollDriver();
	void stopScrollDriver();
	void onScrollTick();
	// `max(0, contentExtent - bandExtent)` on the mode's pan axis; 0 means the band stands still.
	double scrollTravel(MascotPanel panel, const MascotBand& band) const;
	static ScrollDirection scrollDirectionFromString(const QString& value, bool* ok);

	void nextImage(MascotSide side, bool updateHighlight = false);
	// A new left media plus either the same media mirrored or an independently picked one.
	void nextImages(bool forcedMirror);
	void nextBandImages(int bandIndex, bool forcedMirror);

	void flipSide(MascotSide side);
	void flipBoth();
	// The side-directory rule: a file under `right` is drawn mirrored on the left panel and vice
	// versa.
	bool flipForSide(const QString& path, MascotSide side) const;
	color_area colorFor(MascotSide side, bool weighted);

	// Unflipped, and mirrored onto the other panel when the mirror key is on. Gated on debug mode.
	void showDroppedImage(MascotSide side, const QString& path);

signals:
	void clicked(MascotSide side, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	// Emitted only when the swap asked for the theme colour to follow.
	void themeColorChanged(color_area color);

protected:
	// The host receives its new width from the layout after applyMascotWidth() returns, so
	// re-laying out from here would use the old rectangle.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct PanelLayout {
		MascotMode mode = MascotMode::Static;
		// The shipped default and what every existing panel draws, not the spec table's `Crop`.
		MascotSurface::FillMode fill = MascotSurface::FillMode::HeightFit;
		MascotMode appliedMode = MascotMode::Static;
		bool applied = false;
		StripesOrientation orientation = StripesOrientation::Horizontal;
		// Compared too: an axis change alters the rectangles without changing the band count.
		StripesOrientation appliedOrientation = StripesOrientation::Horizontal;
		bool alternate = true;
		// A negative value clamps to 0 rather than reversing: the direction key alone decides the
		// sense.
		double scrollSpeed = 40;
		ScrollDirection scrollDirection = ScrollDirection::LeftToRight;
		bool scrollPingpong = false;
	};

	struct PanelState {
		// QPointer because a surface can also die with its host; non-null exactly while this panel
		// is part of the sheet.
		QPointer<MascotSurface> surface;
		// The first slot this panel owns in the sheet: 0, 8 or 16, fixed for life, so a band's slot
		// index never moves when another panel's band count changes. A band's players are bound to
		// one slot's QVideoSinks, and moving it under them detaches it.
		int slotBase = 0;
		std::vector<std::unique_ptr<MascotBand>> bands;
		MascotBand* band(int i) const;
		int configuredBandCount = -1;
		// Travel lengths: 0..1 one pass, 1..2 the return leg a pingpong folds back. A fraction
		// rather than a pixel offset is what makes a resize rescale the pan and a stall unable to
		// move a band by more than one travel.
		double scrollPhase = 0.0;
		// A marquee of N cells: a pixel displacement that wraps at `N * cellLen`.
		double marqueeOffset = 0.0;
		double marqueePingpongSign = 1.0;
		double marqueeLegDistance = 0.0;
		QVector<bool> scrollZeroTravelLogged;
		bool flipToggled = false;
		// Set by a dropped/debugged file, which bypasses the side-directory rule.
		bool mirrorOverrideEnabled = false;
		bool mirrorOverrideValue = false;
		bool highlightOnApply = false;

		// Panel-wide statements, so N stripes share one line instead of writing N.
		qint64 lastTypeLogMs = 0;
		// Its own rate limit, or a deferral would suppress the "which branch picked" line.
		qint64 lastPreviewDeferLogMs = 0;
	};

	PanelState& stateFor(MascotPanel panel);
	const PanelState& stateFor(MascotPanel panel) const;
	static MascotPanel panelOf(MascotSide side);
	PanelLayout& layoutFor(MascotPanel panel);
	const PanelLayout& layoutFor(MascotPanel panel) const;
	int stripesCountFor(MascotPanel panel) const;
	bool pairSideOf(const PanelState& state, MascotSide* side) const;
	static MascotSide other(MascotSide side);

	bool mirrorEnabled() const;
	// Clamped to the crop's slack: a larger shift slides an edge into view.
	bool centerContentEnabled() const;
	void applySeedSource();
	QString nextSourcePath();

	// ---- one scene for the three layers ----
	// The layer's slots, the veil rectangle and both columns' slots in that z order. The hosts host
	// nothing; a panel's rectangle is translated from its host into the sheet's coordinates.
	QPointer<MascotSurface> sheet;
	void createSheet();
	// Players are shut down while the delegates they draw into still exist, then slots and pins,
	// then the surface.
	void destroySheet();
	// Exists if and only if `running && (mascots || mascots_background)`.
	void syncSheet();
	void attachPanel(PanelState& state, MascotPanel panel);
	void detachPanel(PanelState& state);
	// The switch went off: shutdown, clearSide and detachPanel, in that order.
	void dropPanel(PanelState& state);
	MascotSurface::SlotGroup slotGroupFor(MascotPanel panel) const;
	// The columns are at z 2 and the layer at z 0, so the veil moves between 1 and 3.
	bool mascotsBelowVeil() const;
	void applySheetSlots();
	// The host's rectangle in the sheet's coordinates; a static band is the whole frame, a stripe a
	// slice of it.
	QRectF panelFrame(MascotPanel panel) const;
	void updateSheetVisibility();
	// The backdrop is the window's colour because a QQuickWidget blits opaquely and would turn
	// transparent pixels black; at alpha 1.00 the window must match the layer switched off.
	void pushVeilState();
	bool bandForSlot(int slot, MascotPanel* panel, int* bandIndex) const;

	void syncBands(PanelState& state, MascotPanel panel);
	void fillEmptyBands();

	void updateScrollDriver();
	bool scrollShouldRun() const;
	// Only the cheap reasons: the covered-window test walks every top-level window and builds GDI
	// regions, more work than a whole tick, so it is event-driven.
	bool scrollCanTick() const;
	bool panelsVisible() const;
	bool anySurfaceVisible() const;
	bool panModeActive() const;
	// Perpendicular to the stacking axis: vertical for a vertical `Stripes`, always horizontal for
	// `Scroll`.
	bool scrollPanIsVertical(MascotPanel panel) const;
	bool scrollDirectionReverses(MascotPanel panel) const;
	bool scrollDirectionIsVertical(MascotPanel panel) const;
	double scrollPhaseOffset(MascotPanel panel, const MascotBand& band) const;
	bool scrollReversed(MascotPanel panel, const MascotBand& band) const;
	// One panel at a time: the surface's mirror keeps the last offset and applySlot() would replay
	// it.
	void resetScrollOffsets(MascotPanel panel);
	void logScroll(const QString& message) const;
	void clearSide(PanelState& state);
	// Owners are mutated first, so a candidate becoming the displayed media is never unpinned.
	bool pinReferenced(const QString& path) const;
	void syncPin(const QString& path);
	static MascotSurface::FillMode fillModeFromString(const QString& value, bool* ok);
	// A vertical `Stripes` needs `WidthFit` or every band's content would fit its height exactly and
	// the travel would be 0.
	MascotSurface::FillMode effectiveFillMode(MascotPanel panel) const;
	void applyDisplay(PanelState& state, MascotPanel panel);
	// The mirror, then the fill mode, then the media: the media write must never land under the old
	// mirror.
	void presentMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, int bandIndex);
	bool mirrorParityFor(MascotPanel panel, const QString& path) const;
	void applyMirror(PanelState& state, MascotPanel panel, const QString& path, int bandIndex);
	void requestPath(PanelState& state, const QString& path, int bandIndex);
	// The panel-wide policy -- flip parity, the mirror pair, the theme colour -- is band 0's only.
	void showMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, bool fromReady,
		bool armCandidate, int bandIndex);
	void holdCandidate(MascotBand& band, const MascotMedia& media);
	void speculate(PanelState& state, MascotPanel panel, int bandIndex);
	void armSpeculation(MascotPanel panel, int bandIndex);
	// An empty media means the pick is in flight and will arrive through mediaReady.
	MascotMedia pickFor(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	void preparePick(PanelState& state, int bandIndex, bool updateHighlight);

	void connectBeats();
	void disconnectBeats();
	void onBeat(bool swapWanted);

	int clipSecondsFor(MascotPanel panel) const;
	// False means there is no pass to defer to, so the change is committed now.
	bool bandReachesSeam(MascotPanel panel, const MascotBand& band) const;
	void scheduleBeatAdvance(MascotPanel panel, int bandIndex);
	void commitBeatAdvance(MascotPanel panel, int bandIndex);
	bool beatsAllowed() const;

	void onMediaReady(const MascotMedia& media);
	void onMediaFailed(const QString& path);

	// The clamp degrades rather than fails: one player with a hard cut, then no video at all.
	enum class VideoMode { Crossfade, SinglePlayer, Off };

	MascotMedia::Type pickType(PanelState& state, MascotPanel panel);
	bool mediaSettingWantsVideo() const;
	bool mediaMayBeVideo(MascotPanel panel) const;
	void invalidateVideoPoolIfStale();

	void startVideoLoad(PanelState& state, MascotPanel panel, int bandIndex, const QString& path,
		qint64 offsetMs);
	enum class VideoPick { Started, Skip, Fallback };

	VideoPick pickVideo(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	// The band index is carried so a frame can only ever be committed to the band that asked.
	void onVideoFirstFrame(MascotPanel panel, int bandIndex, const QString& path, int ab,
		const QSize& frameSize, qint64 offsetMs);
	void onVideoFailed(MascotPanel panel, int bandIndex, const QString& path,
		MascotVideoSlot::FailKind kind, bool timedOut);
	void fadeInVideo(PanelState& state, MascotPanel panel, int bandIndex, int ab);
	void startMirrorVideo(int bandIndex, const QString& path, qint64 offsetMs);

	// Queued: it reads the pool and posts a load, which the commit must not pay for inline.
	void preloadNext(MascotPanel panel);
	void startPreload(MascotPanel panel);
	void onVideoPreloaded(MascotPanel panel, int bandIndex, const QString& path, qint64 offsetMs,
		const QSize& frameSize);

	bool ensureVideoSlots();
	void createVideoSlot(PanelState& state, MascotPanel panel, int bandIndex);
	void destroyVideoSlot(PanelState& state, int bandIndex);
	void destroyVideoSlot(PanelState& state);
	void applyDecoderBudget();
	void applyBandBudget();
	// Columns first, background last.
	void applyBackgroundBudget();
	// 0 with no player, 1 for `Static`/`Scroll`, `min(stripes, players)` for `Stripes`.
	int backgroundBandCount() const;
	// One funded band is not a marquee: dividing by `N - 1` would divide by zero, and a single cell
	// is the same picture with no translation.
	bool backgroundMarqueeActive(MascotPanel panel) const;
	bool bandMayOwnPlayer(MascotPanel panel, int bandIndex) const;
	// Only `Static` fades; the panning modes give a band one player and take the hard cut.
	bool videoCrossfades(MascotPanel panel) const;
	bool stripesStackVertically(MascotPanel panel) const;
	void restartClipTimer(MascotPanel panel, int bandIndex);
	void onClipTimer(MascotPanel panel, int bandIndex);
	void stopClipTimers();
	bool bandMirrorsLeft(int bandIndex) const;
	// Queued on `this` so it never recurses out of a player's signal.
	void queueBandRefill(MascotPanel panel, int bandIndex, bool onlyIfEmpty);

	void applyVideoPaused();
	bool videoShouldPause() const;
	// `utils::IsMyWindowVisible()` is a GDI walk over every top-level window, affordable at a beat
	// and an Apply but not per frame, and the covered case produces no event: asked on a one-second
	// timer plus every window event and cached for videoShouldPause().
	void refreshWindowSeen();
	bool windowSeen = true;
	bool windowSeenKnown = false;
	QTimer windowSeenTimer;
	bool previewTooltipBusy() const;
	void watchWindow(QWidget* window);

	// Debug-gated the way MainApp's MASCOT-CACHE line is: an ordinary run pays nothing.
	void logVideo(const QString& message) const;
	void logBackground(const QString& message) const;

	MainApp* App = nullptr;
	MascotImageCache* cache = nullptr;
	QPointer<MascotPrefetcher> prefetcher;
	QPointer<QWidget> leftHost;
	QPointer<QWidget> rightHost;
	QPointer<QWidget> backgroundHost;
	MascotMediaSource source;
	PanelState leftState;
	PanelState rightState;
	PanelState backgroundState;
	bool running = false;
	// Non-zero while a wrap-point commit a band queued is running: the commit must not tear the
	// layout down from inside the scroll driver's band loop, so commits run after it and applyMode()
	// re-queues itself while this is non-zero.
	int wrapCommitDepth = 0;
	// An unreadable config value is a statement about the configuration, so it is reported once.
	bool warnedUnsupportedMedia = false;
	bool warnedUnsupportedMode = false;
	bool warnedUnsupportedFill = false;
	bool warnedUnsupportedStripesOrientation = false;
	bool warnedUnsupportedBackgroundMode = false;
	bool warnedUnsupportedBackgroundFill = false;
	bool warnedUnsupportedBackgroundStripesOrientation = false;
	bool warnedUnsupportedBackgroundScrollDirection = false;
	QMetaObject::Connection beatConnection;

	PanelLayout columnsLayout;
	PanelLayout backgroundLayout;
	// Clamped to [1, 8]; only `Stripes` reads the stripe counts.
	int stripesLeftBands = 2;
	int stripesRightBands = 2;
	int backgroundStripes = 3;
	bool backgroundEnabled = false;
	bool columnsEnabled = true;
	// What is left of `mascots_max_decoders` after the pair's entitlement.
	int backgroundPlayers = 0;
	bool backgroundCrossfades = false;
	int loggedBackgroundDecoders = -1;
	int loggedBackgroundPlayers = -1;
	int loggedBackgroundBands = -1;
	MascotMode loggedBackgroundMode = MascotMode::Static;
	bool backgroundEmptyPoolLogged = false;
	// A prefix of the band list: indices below the count keep (or get) a player.
	int leftBandPlayers = 0;
	int rightBandPlayers = 0;
	// The whole tuple, split included, because two allocations can keep the same total on different
	// panels.
	int loggedBandBudgetDecoders = -1;
	int loggedBandBudgetWanted = -1;
	int loggedBandBudgetAllowed = -1;
	int loggedBandBudgetLeft = -1;
	int loggedBandBudgetRight = -1;
	MascotMode loggedBandBudgetMode = MascotMode::Static;

	// As the user wrote it, so a line can echo the configuration rather than a normalised spelling.
	QString scrollDirectionText = QStringLiteral("LeftToRight");
	QString loggedScrollDirection;
	bool warnedUnsupportedScrollDirection = false;
	// Created on the first start, so a config that never scrolls allocates nothing.
	QTimer* scrollTimer = nullptr;
	QElapsedTimer scrollClock;

	QString mediaSetting = QStringLiteral("Images");
	int clipSeconds = 30;
	// Pushed into every slot at creation; the intent is fixed for a player's lifetime.
	bool lowLatency = true;
	bool pairScrollOpposite = false;
	// Whether the right column is drawn as the left's mirror or repeats it.
	bool flipMirroredColumn = true;
	// Enabling the mirror key re-pairs the columns once; `known` keeps the first Apply of a run,
	// before the panels have filled, from counting as a transition.
	bool mirrorAppliedLast = false;
	bool mirrorAppliedKnown = false;
	// The layer's own cadence, each falling back to its columns twin when absent.
	int backgroundClipSeconds = 30;
	bool backgroundSwapOnBeat = true;
	bool backgroundRandomChange = true;
	double backgroundRandomChance = 0.1;
	bool swapOnBeat = true;
	bool randomStart = true;
	int crossfadeMs = 600;
	int maxDecoders = 6;
	VideoMode videoMode = VideoMode::Crossfade;
	int loggedDecoderBudget = -1;
	// Set by the first pick that found the sink missing or gave up waiting: a wiring fact.
	bool warnedVideoSink = false;
	bool warnedVideoSinkGiveUp = false;
	bool pauseUnfocused = true;
	bool videoPaused = false;
	bool weightedRandom = true;
	bool preloadEnabled = true;
	// A machine-speed judgement; clamped here and again in MascotVideoSlot::setTimings().
	int swapTimeoutMs = 5000;
	int retryIntervalMs = 150;
	int retryAttempts = 5;
	int busyDeferMs = 250;
	QPointer<QWidget> watchedWindow;
	bool sawWindowActivation = false;
	// Each is cheap to compare and each is a reason for the cached scan to drop.
	bool poolFingerprintValid = false;
	QString poolDb;
	int poolRowCount = -1;
	QJsonObject poolFilterJson;

};
