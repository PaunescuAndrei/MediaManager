#pragma once
#include "MascotMedia.h"
#include "MascotMediaSource.h"
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

enum class MascotSide { Left, Right };

// The only conversion from a side is panelOf(); there is no inverse, because the background belongs to no pair.
enum class MascotPanel { Left, Right, Background };

enum class MascotMode { Static, Stripes };

enum class ScrollDirection { LeftToRight, RightToLeft, TopToBottom, BottomToTop };

enum class StripesOrientation { Horizontal, Vertical };

// The three layers share one MascotSurface scene: separate QQuickWidgets cannot be ordered, because one carrying
// Qt::WA_AlwaysStackOnTop draws above every sibling. GUI thread only, the prefetcher's signals included.
class MascotController : public QObject {
	Q_OBJECT
public:
	MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
		QWidget* leftHost, QWidget* rightHost, QWidget* backgroundHost, QObject* parent = nullptr);
	~MascotController() override;

	void start();
	void stop();
	bool isRunning() const;
	void stopPanels();

	void applyConfig();
	void updateLayout();
	void pushVeil();

	static MascotMode modeFromString(const QString& value, bool* ok);
	static StripesOrientation stripesOrientationFromString(const QString& value, bool* ok);

	int bandCount(MascotPanel panel) const;
	QVector<QRectF> bandRects(MascotPanel panel) const;
	int videoBandsFor(MascotPanel panel) const;
	void applyMode();

	// The phase is a fraction of a travel wrapped with fmod, so a resize rescales the pan and a stall moves a band by at most one travel.
	void startScrollDriver();
	void stopScrollDriver();
	void onScrollTick();
	double scrollTravel(MascotPanel panel, const MascotBand& band) const;
	static ScrollDirection scrollDirectionFromString(const QString& value, bool* ok);

	void nextImages(bool forcedMirror);
	void nextBandImages(int bandIndex, bool forcedMirror);
	// A mirrored pair shows one clip per band, so either call advances the pair's band, never half of it.
	void advanceBand(MascotSide side, int bandIndex);
	void advanceStrip(MascotSide side);

	void flipSide(MascotSide side);
	void flipBoth();
	// A file under `right` is drawn mirrored on the left panel and vice versa.
	bool flipForSide(const QString& path, MascotSide side) const;
	color_area colorFor(MascotSide side, bool weighted);
	color_area colorForBand(MascotSide side, int bandIndex, bool weighted);

	void showDroppedImage(MascotSide side, const QString& path);

signals:
	// `bandIndex` is the stripe the click landed on; the side alone would make every band act on the first.
	void clicked(MascotSide side, int bandIndex, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	void themeColorChanged(color_area color);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct PanelLayout {
		MascotMode mode = MascotMode::Static;
		MascotSurface::FillMode fill = MascotSurface::FillMode::HeightFit;
		MascotMode appliedMode = MascotMode::Static;
		bool applied = false;
		// Independent of `mode`: one band or several can pan, and `Scroll` is not a layout of its own.
		bool pan = false;
		bool appliedPan = false;
		StripesOrientation orientation = StripesOrientation::Horizontal;
		StripesOrientation appliedOrientation = StripesOrientation::Horizontal;
		bool alternate = true;
		double scrollSpeed = 40;
		ScrollDirection scrollDirection = ScrollDirection::LeftToRight;
		bool scrollPingpong = false;
	};

	struct PanelState {
		// QPointer because a surface can also die with its host.
		QPointer<MascotSurface> surface;
		// The first slot this panel owns in the sheet: 0, 8 or 16, fixed for life. A band's players are
		// bound to one slot's QVideoSinks, so its index must never move when another panel's count changes.
		int slotBase = 0;
		std::vector<std::unique_ptr<MascotBand>> bands;
		MascotBand* band(int i) const;
		int configuredBandCount = -1;
		double scrollPhase = 0.0;
		// A marquee of cells translated by a pixel offset, not content panned inside a fixed cell.
		double marqueeOffset = 0.0;
		double marqueePingpongSign = 1.0;
		double marqueeLegDistance = 0.0;
		QVector<bool> scrollZeroTravelLogged;
		bool flipToggled = false;
		bool mirrorOverrideEnabled = false;
		bool mirrorOverrideValue = false;
		bool highlightOnApply = false;

		qint64 lastTypeLogMs = 0;
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
	bool centerContentEnabled() const;
	void applySeedSource();
	QString nextSourcePath();

	QPointer<MascotSurface> sheet;
	void createSheet();
	// Every band's players are shut down while the delegates they draw into still exist.
	void destroySheet();
	void syncSheet();
	void attachPanel(PanelState& state, MascotPanel panel);
	void detachPanel(PanelState& state);
	void dropPanel(PanelState& state);
	MascotSurface::SlotGroup slotGroupFor(MascotPanel panel) const;
	// Columns at z 2, layer at z 0: a veil at z 1 sits under the columns, one at z 3 over them.
	bool mascotsBelowVeil() const;
	// The window-sized host is the sheet's parent, so its frame is the origin and its size, not its pos().
	QRectF panelFrame(MascotPanel panel) const;
	void updateSheetVisibility();
	// The backdrop is the window's colour because the sheet blits opaquely and would otherwise turn
	// transparent pixels black; a veil at alpha 1.00 must read exactly like the layer switched off.
	void pushVeilState();
	bool bandForSlot(int slot, MascotPanel* panel, int* bandIndex) const;

	void syncBands(PanelState& state, MascotPanel panel);
	void fillEmptyBands();

	void updateScrollDriver();
	bool scrollShouldRun() const;
	bool scrollCanTick() const;
	bool panelsVisible() const;
	bool anySurfaceVisible() const;
	bool panModeActive() const;
	bool scrollDirectionReverses(MascotPanel panel) const;
	bool scrollDirectionIsVertical(MascotPanel panel) const;
	double scrollPhaseOffset(MascotPanel panel, const MascotBand& band) const;
	bool scrollReversed(MascotPanel panel, const MascotBand& band) const;
	// The surface's mirror keeps the last offset and applySlot() would replay it.
	void resetScrollOffsets(MascotPanel panel);
	void logScroll(const QString& message) const;
	void clearSide(PanelState& state);
	// Owners are mutated first and changed paths re-evaluated, so a candidate becoming the displayed media is never unpinned.
	bool pinReferenced(const QString& path) const;
	void syncPin(const QString& path);
	static MascotSurface::FillMode fillModeFromString(const QString& value, bool* ok);
	// The mode the user picked is the framing in every panel; what may travel is decided separately.
	MascotSurface::FillMode effectiveFillMode(MascotPanel panel) const;
	void applyDisplay(PanelState& state, MascotPanel panel);
	void presentMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, int bandIndex);
	void applyMirror(PanelState& state, MascotPanel panel, const QString& path, int bandIndex);
	void requestPath(PanelState& state, const QString& path, int bandIndex);
	void showMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, bool fromReady,
		bool armCandidate, int bandIndex);
	void holdCandidate(MascotBand& band, const MascotMedia& media);
	void speculate(PanelState& state, MascotPanel panel, int bandIndex);
	void armSpeculation(MascotPanel panel, int bandIndex);
	MascotMedia pickFor(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	void preparePick(PanelState& state, int bandIndex, bool updateHighlight);

	void connectBeats();
	void disconnectBeats();
	void onBeat(bool swapWanted);

	int clipSecondsFor(MascotPanel panel) const;
	bool bandReachesSeam(MascotPanel panel, const MascotBand& band) const;
	void scheduleBeatAdvance(MascotPanel panel, int bandIndex);
	void commitBeatAdvance(MascotPanel panel, int bandIndex);
	bool beatsAllowed() const;

	void onMediaReady(const MascotMedia& media);
	void onMediaFailed(const QString& path);

	enum class VideoMode { Crossfade, SinglePlayer, Off };

	MascotMedia::Type pickType(PanelState& state, MascotPanel panel);
	bool mediaSettingWantsVideo() const;
	bool mediaMayBeVideo(MascotPanel panel) const;
	void invalidateVideoPoolIfStale();

	void startVideoLoad(PanelState& state, MascotPanel panel, int bandIndex, const QString& path,
		qint64 offsetMs);
	enum class VideoPick { Started, Skip, Fallback };

	VideoPick pickVideo(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	void onVideoFirstFrame(MascotPanel panel, int bandIndex, const QString& path, int ab,
		const QSize& frameSize, qint64 offsetMs);
	void onVideoFailed(MascotPanel panel, int bandIndex, const QString& path,
		MascotVideoSlot::FailKind kind, bool timedOut);
	void fadeInVideo(PanelState& state, MascotPanel panel, int bandIndex, int ab);

	void preloadNext(MascotPanel panel);
	// Under the mirror the pair shares one cadence: the left band's fade is what drives the right panel's preload too.
	void preloadPairPanel(MascotPanel panel);
	void startPreload(MascotPanel panel);
	void onVideoPreloaded(MascotPanel panel, int bandIndex, const QString& path, qint64 offsetMs,
		const QSize& frameSize);

	bool ensureVideoSlots();
	void createVideoSlot(PanelState& state, MascotPanel panel, int bandIndex);
	void destroyVideoSlot(PanelState& state, int bandIndex);
	void destroyVideoSlot(PanelState& state);
	void applyDecoderBudget();
	void applyBandBudget();
	void applyBackgroundBudget();
	int backgroundBandCount() const;
	// One funded band is not a marquee: `count - 1` divides by zero, and one cell is the same picture untranslated.
	bool backgroundMarqueeActive(MascotPanel panel) const;
	bool bandMayOwnPlayer(MascotPanel panel, int bandIndex) const;
	bool videoCrossfades(MascotPanel panel) const;
	bool stripesStackVertically(MascotPanel panel) const;
	void restartClipTimer(MascotPanel panel, int bandIndex);
	void onClipTimer(MascotPanel panel, int bandIndex);
	void stopClipTimers();
	bool bandMirrorsLeft(int bandIndex) const;
	// True while `panel`'s band draws the left band's stage: it then needs no player of its own.
	bool bandUsesMirrorView(MascotPanel panel, int bandIndex) const;
	void syncMirrorBindings();
	void queueBandRefill(MascotPanel panel, int bandIndex, bool onlyIfEmpty);

	void applyVideoPaused();
	bool videoShouldPause() const;
	// `IsMyWindowVisible()` is a GDI walk over every top-level window and the covered case produces no
	// event, so it is asked on a one-second timer and cached.
	void refreshWindowSeen();
	bool windowSeen = true;
	bool windowSeenKnown = false;
	QTimer windowSeenTimer;
	bool previewTooltipBusy() const;
	void watchWindow(QWidget* window);

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
	// Non-zero while a wrap-point commit runs: it must not tear the layout down from inside the scroll
	// loop, so commits run after it and applyMode() re-queues.
	int wrapCommitDepth = 0;
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
	int stripesLeftBands = 2;
	int stripesRightBands = 2;
	int backgroundStripes = 3;
	bool backgroundEnabled = false;
	bool columnsEnabled = true;
	// bandCount(Background) derives the layer's bands from this, so a shrinking budget retires bands.
	int backgroundPlayers = 0;
	bool backgroundCrossfades = false;
	int loggedBackgroundDecoders = -1;
	int loggedBackgroundPlayers = -1;
	int loggedBackgroundBands = -1;
	MascotMode loggedBackgroundMode = MascotMode::Static;
	bool backgroundEmptyPoolLogged = false;
	int leftBandPlayers = 0;
	int rightBandPlayers = 0;
	int loggedBandBudgetDecoders = -1;
	int loggedBandBudgetWanted = -1;
	int loggedBandBudgetAllowed = -1;
	int loggedBandBudgetLeft = -1;
	int loggedBandBudgetRight = -1;
	MascotMode loggedBandBudgetMode = MascotMode::Static;

	QString scrollDirectionText = QStringLiteral("LeftToRight");
	bool warnedUnsupportedScrollDirection = false;
	QTimer* scrollTimer = nullptr;
	QElapsedTimer scrollClock;

	QString mediaSetting = QStringLiteral("Images");
	int clipSeconds = 30;
	bool lowLatency = true;
	bool pairScrollOpposite = false;
	bool flipMirroredColumn = true;
	bool mirrorAppliedLast = false;
	bool mirrorAppliedKnown = false;
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
	bool warnedVideoSink = false;
	bool warnedVideoSinkGiveUp = false;
	bool pauseUnfocused = true;
	bool videoPaused = false;
	bool weightedRandom = true;
	bool preloadEnabled = true;
	int swapTimeoutMs = 5000;
	int retryIntervalMs = 150;
	int retryAttempts = 5;
	int busyDeferMs = 250;
	QPointer<QWidget> watchedWindow;
	bool poolFingerprintValid = false;
	QString poolDb;
	int poolRowCount = -1;
	QJsonObject poolFilterJson;

};
