#include "stdafx.h"
#include "MascotController.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "PreviewVideoWindow.h"
#include "VideosModel.h"
#include "utils.h"
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QPalette>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
	const QString VIDEO_LOG_TAG = QStringLiteral("MASCOT-VIDEO: ");
	const QString SCROLL_LOG_TAG = QStringLiteral("MASCOT-SCROLL: ");
	const QString BACKGROUND_LOG_TAG = QStringLiteral("MASCOT-BG: ");

	constexpr int VIDEO_PANEL_COUNT = 2;
	constexpr int PLAYERS_PER_PANEL = 2;

	constexpr int MAX_CROSSFADE_MS = 10000;
	constexpr int MAX_CLIP_SECONDS = 86400;
	constexpr int MAX_CLIP_TIMER_MS = MAX_CLIP_SECONDS * 1000;

	// A panel failing this many times in a row stops re-arming: a library of undecodable files would otherwise be walked at load speed.
	constexpr int VIDEO_FAILURE_LIMIT = 3;



	constexpr qint64 VIDEO_LOG_INTERVAL_MS = 5000;

	constexpr int PANEL_SLOT_STRIDE = 8;
	constexpr int LEFT_SLOT_BASE = 0;
	constexpr int RIGHT_SLOT_BASE = LEFT_SLOT_BASE + PANEL_SLOT_STRIDE;
	constexpr int BACKGROUND_SLOT_BASE = RIGHT_SLOT_BASE + PANEL_SLOT_STRIDE;
	// Every slot the three bases can address, created once when the sheet is built and never changed afterwards.
	// A QQuickRepeater rebuilds all of its delegates when its model changes, and each rebuild destroys the
	// QVideoSink of every slot that already existed. A player that has started keeps the sink it was handed, so a
	// rebuild orphans it: it goes on buffering and never presents a frame again.
	constexpr int SLOT_CAPACITY = BACKGROUND_SLOT_BASE + PANEL_SLOT_STRIDE;

	QString sideLabel(MascotSide side)
	{
		return side == MascotSide::Left ? QStringLiteral("left") : QStringLiteral("right");
	}

	QString panelLabel(MascotPanel panel)
	{
		switch (panel) {
		case MascotPanel::Left:
			return sideLabel(MascotSide::Left);
		case MascotPanel::Right:
			return sideLabel(MascotSide::Right);
		case MascotPanel::Background:
			break;
		}
		return QStringLiteral("background");
	}

	QString bandLabel(MascotPanel panel, int bandIndex)
	{
		if (bandIndex == 0)
			return panelLabel(panel);
		return QStringLiteral("%1 band %2").arg(panelLabel(panel)).arg(bandIndex);
	}

	int clampedCrossfadeMs(Config* config)
	{
		if (!config)
			return 600;
		const int value = config->get(QStringLiteral("mascots_crossfade_ms")).toInt();
		return std::clamp(value, 0, MAX_CROSSFADE_MS);
	}

	int clampedClipSeconds(Config* config)
	{
		if (!config)
			return 30;
		const int value = config->get(QStringLiteral("mascots_clip_seconds")).toInt();
		return std::clamp(value, 0, MAX_CLIP_SECONDS);
	}

	// A value that does not parse falls back to `fallback`, never to a floor: clamped to [low, high], a
	// typo like "abc" would become the most aggressive setting and abandon clips that are merely opening.
	int clampedConfigInt(Config* config, const QString& key, int fallback, int low, int high)
	{
		if (!config)
			return fallback;
		bool ok = false;
		const int value = config->get(key).toInt(&ok);
		return std::clamp(ok ? value : fallback, low, high);
	}

	// Config::get_bool() has no fallback and answers False for an absent key, which would switch a pre-existing config off.
	bool configBoolWithFallback(Config* config, const QString& key, bool fallback)
	{
		if (!config)
			return fallback;
		const QString value = config->get(key);
		return value.isEmpty() ? fallback : utils::text_to_bool(value.toStdString());
	}

	// A slice's end is the next slice's start, rounded to whole pixels, so the slices sum to the panel exactly;
	// a height by division leaves the last band a pixel short and shows a seam.
	QVector<QRectF> slicedBandRects(const QRectF& panel, int count, bool stackVertically)
	{
		QVector<QRectF> rects;
		if (count <= 0 || panel.width() <= 0.0 || panel.height() <= 0.0)
			return rects;
		if (count == 1) {
			rects.push_back(panel);
			return rects;
		}
		rects.reserve(count);
		for (int i = 0; i < count; ++i) {
			if (stackVertically) {
				const double top = std::round(panel.top() + panel.height() * i / count);
				const double bottom = std::round(panel.top() + panel.height() * (i + 1) / count);
				rects.push_back(QRectF(panel.left(), top, panel.width(), bottom - top));
			}
			else {
				const double left = std::round(panel.left() + panel.width() * i / count);
				const double right = std::round(panel.left() + panel.width() * (i + 1) / count);
				rects.push_back(QRectF(left, panel.top(), right - left, panel.height()));
			}
		}
		return rects;
	}

	QRectF marqueeBandRect(const QRectF& panel, int count, int index, double offset, bool vertical)
	{
		const double axisLen = vertical ? panel.height() : panel.width();
		const double cellLen = axisLen / static_cast<double>(count - 1);
		const double total = static_cast<double>(count) * cellLen;
		double wrapped = std::fmod(static_cast<double>(index) * cellLen + offset, total);
		if (wrapped < 0.0)
			wrapped += total;
		const double pos = wrapped - cellLen;
		if (vertical)
			return QRectF(panel.left(), panel.top() + pos, panel.width(), cellLen);
		return QRectF(panel.left() + pos, panel.top(), cellLen, panel.height());
	}


	// A precise timer is the one Qt will not coalesce into 15 ms/1 s buckets, which at 40 px/s would stutter.
	constexpr int SCROLL_TICK_MS = 16;

	// The floor is 0 rather than |value|: a negative speed must not invert the direction the user asked for.
	constexpr int MAX_SCROLL_SPEED = 2000;

	constexpr double SCROLL_PHASE_SPAN = 2.0;

	double advancedScrollPhase(double phase, double delta)
	{
		double next = std::fmod(phase + delta, SCROLL_PHASE_SPAN);
		if (next < 0.0)
			next += SCROLL_PHASE_SPAN;
		return next;
	}

	double scrollFractionFor(double phase, double offset, bool pingpong)
	{
		const double span = pingpong ? SCROLL_PHASE_SPAN : 1.0;
		double unit = std::fmod(phase + offset, span);
		if (unit < 0.0)
			unit += span;
		if (!pingpong)
			return unit;
		return unit <= 1.0 ? unit : SCROLL_PHASE_SPAN - unit;
	}

	double scrollOffsetFor(double fraction, double travel, bool reverse)
	{
		const double travelled = reverse ? 1.0 - fraction : fraction;
		return (0.5 - travelled) * travel;
	}

	QString scrollSenseLabel(bool reverse)
	{
		return reverse ? QStringLiteral("right to left") : QStringLiteral("left to right");
	}


	QString modeLabel(MascotMode mode)
	{
		switch (mode) {
		case MascotMode::Scroll:
			return QStringLiteral("Scroll");
		case MascotMode::Stripes:
			return QStringLiteral("Stripes");
		case MascotMode::Static:
			break;
		}
		return QStringLiteral("Static");
	}

	bool windowOnScreen(const QWidget* window)
	{
		return window && window->isVisible() && !window->isMinimized();
	}
}

MascotController::MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
	QWidget* leftHost, QWidget* rightHost, QWidget* backgroundHost, QObject* parent)
	: QObject(parent), source(App)
{
	this->App = App;
	this->cache = cache;
	this->prefetcher = prefetcher;
	this->leftHost = leftHost;
	this->rightHost = rightHost;
	this->backgroundHost = backgroundHost;

	MascotPrefetcher* source = this->prefetcher.data();
	if (source) {
		connect(source, &MascotPrefetcher::mediaReady, this, &MascotController::onMediaReady);
		connect(source, &MascotPrefetcher::mediaFailed, this, &MascotController::onMediaFailed);
	}

	this->windowSeenTimer.setInterval(1000);
	this->windowSeenTimer.setTimerType(Qt::CoarseTimer);
	connect(&this->windowSeenTimer, &QTimer::timeout, this, &MascotController::refreshWindowSeen);
}

MascotController::~MascotController()
{
	this->stop();
}

void MascotController::start()
{
	const bool pairPresent = this->leftState.surface && this->rightState.surface;
	if (this->running && pairPresent)
		return;
	const bool alreadyRunning = this->running;
	// Config first: createSheet() applies it to the slots it builds.
	this->applyConfig();
	this->createSheet();
	if (!alreadyRunning) {
		this->running = true;
		// After `running`: the sheet exists iff running and a switch is on, and applyConfig() ran while it was false.
		this->syncSheet();
		this->connectBeats();
	}
	this->windowSeenKnown = false;
	this->refreshWindowSeen();
	this->windowSeenTimer.start();
	if (this->columnsEnabled) {
		this->nextImages(false);
		if (this->bandCount(MascotPanel::Left) > 1 || this->bandCount(MascotPanel::Right) > 1)
			this->fillEmptyBands();
	}
	if (this->backgroundEnabled && this->backgroundState.surface) {
		const int count = static_cast<int>(this->backgroundState.bands.size());
		for (int i = 0; i < count; ++i) {
			MascotBand* band = this->backgroundState.band(i);
			if (!band || !band->media().path.isEmpty() || !band->requestPath().isEmpty())
				continue;
			this->pickFor(this->backgroundState, MascotPanel::Background, i, i == 0);
		}
	}
	this->updateScrollDriver();
}

void MascotController::stop()
{
	this->running = false;
	// First: a tick that arrived after the surfaces were released would write into a slot being destroyed.
	this->stopScrollDriver();
	this->disconnectBeats();
	this->stopClipTimers();
	this->watchWindow(nullptr);
	this->windowSeenTimer.stop();
	this->windowSeen = true;
	this->windowSeenKnown = false;
	this->videoPaused = false;
	// Players come down before the surfaces: their sinks belong to the surfaces' QML delegates, and
	// shutdown() detaches them synchronously.
	for (PanelState* state : { &this->leftState, &this->rightState, &this->backgroundState }) {
		for (auto& owned : state->bands) {
			if (owned->video())
				owned->video()->shutdown();
		}
	}
	// clear() drops queued notifications and makes in-flight decodes unpublishable, so none can resurrect a slot.
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->clear();
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
	this->clearSide(this->backgroundState);
	this->detachPanel(this->leftState);
	this->detachPanel(this->rightState);
	this->detachPanel(this->backgroundState);
	this->syncSheet();
}

void MascotController::stopPanels()
{
	for (PanelState* state : { &this->leftState, &this->rightState }) {
		for (auto& owned : state->bands) {
			if (owned->video())
				owned->video()->shutdown();
		}
	}
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->clear();
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
	this->detachPanel(this->leftState);
	this->detachPanel(this->rightState);
	// Not syncSheet(): `columnsEnabled` is still true here, so it would re-attach the panels just taken down.
	// updateSheetVisibility() is the only question left.
	this->updateSheetVisibility();
	this->updateScrollDriver();
}

bool MascotController::isRunning() const
{
	return this->running;
}

void MascotController::applyConfig()
{
	if (!this->App || !this->App->config)
		return;
	Config* config = this->App->config;

	this->mediaSetting = config->get(QStringLiteral("mascots_media"));
	this->clipSeconds = clampedClipSeconds(config);
	this->swapOnBeat = config->get_bool(QStringLiteral("mascots_swap_on_beat"));
	this->backgroundClipSeconds = clampedConfigInt(config,
		QStringLiteral("mascots_background_clip_seconds"), 30, 0, MAX_CLIP_SECONDS);
	this->backgroundSwapOnBeat = configBoolWithFallback(config,
		QStringLiteral("mascots_background_swap_on_beat"), true);
	this->backgroundRandomChange = configBoolWithFallback(config,
		QStringLiteral("mascots_background_random_change"), true);
	this->backgroundRandomChance = std::clamp(config->get_double(
		QStringLiteral("mascots_background_random_chance"), 10.0) / 100.0, 0.0, 1.0);
	// A commit queued under the old configuration waits for a seam that may never come (this Apply can switch
	// the mode or the cadence), so every panel drops them here; the new configuration queues its own.
	for (PanelState* state : { &this->leftState, &this->rightState, &this->backgroundState }) {
		for (auto& owned : state->bands) {
			if (owned)
				owned->discardWrapCommit();
		}
	}
	this->randomStart = config->get_bool(QStringLiteral("mascots_random_start"));
	this->pairScrollOpposite = config->get(QStringLiteral("mascots_pair_scroll"))
		.compare(QStringLiteral("Opposite"), Qt::CaseInsensitive) == 0;
	this->flipMirroredColumn = configBoolWithFallback(config, QStringLiteral("mascots_pair_flip"), true);
	this->lowLatency = config->get_bool(QStringLiteral("mascots_low_latency"));
	this->crossfadeMs = clampedCrossfadeMs(config);
	this->maxDecoders = std::max(0, config->get(QStringLiteral("mascots_max_decoders")).toInt());
	this->pauseUnfocused = config->get_bool(QStringLiteral("mascots_pause_unfocused"));
	this->weightedRandom = config->get_bool(QStringLiteral("mascots_weighted_random"));
	this->preloadEnabled = config->get_bool(QStringLiteral("mascots_preload"));
	// Clamped where read so a config cannot abandon a clip that is merely opening: the swap deadline
	// [500, 60000] has no "off" value -- this timer is the only thing that releases a load that never paints.
	this->swapTimeoutMs = clampedConfigInt(config, QStringLiteral("mascots_swap_timeout_ms"), 5000, 500, 60000);
	this->retryIntervalMs = clampedConfigInt(config, QStringLiteral("mascots_retry_interval_ms"), 150, 20, 2000);
	this->retryAttempts = clampedConfigInt(config, QStringLiteral("mascots_retry_attempts"), 5, 0, 50);
	this->busyDeferMs = clampedConfigInt(config, QStringLiteral("mascots_busy_defer_ms"), 250, 50, 5000);

	bool modeOk = false;
	const QString modeValue = config->get(QStringLiteral("mascots_mode"));
	const MascotMode parsedMode = MascotController::modeFromString(modeValue, &modeOk);
	this->columnsLayout.mode = modeOk ? parsedMode : MascotMode::Static;
	this->stripesLeftBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_left"), 2, 1, 8);
	this->stripesRightBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_right"), 2, 1, 8);
	bool orientationOk = false;
	const QString orientationValue = config->get(QStringLiteral("mascots_stripes_orientation"));
	const StripesOrientation parsedOrientation =
		MascotController::stripesOrientationFromString(orientationValue, &orientationOk);
	this->columnsLayout.orientation =
		orientationOk ? parsedOrientation : StripesOrientation::Horizontal;
	this->columnsLayout.alternate = config->get_bool(QStringLiteral("mascots_stripes_alternate"));
	bool fillOk = false;
	const QString fillValue = config->get(QStringLiteral("mascots_fill"));
	const MascotSurface::FillMode parsedFill = MascotController::fillModeFromString(fillValue, &fillOk);
	this->columnsLayout.fill = fillOk ? parsedFill : MascotSurface::FillMode::HeightFit;

	if (!this->warnedUnsupportedMedia && !this->mediaSetting.isEmpty()
		&& this->mediaSetting.compare(QStringLiteral("Images"), Qt::CaseInsensitive) != 0
		&& this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) != 0
		&& this->mediaSetting.compare(QStringLiteral("Mixed"), Qt::CaseInsensitive) != 0) {
		this->warnedUnsupportedMedia = true;
		this->logVideo(QStringLiteral("mascots_media=\"%1\" is not one of Images, Videos or Mixed; showing images.")
			.arg(this->mediaSetting));
		// The ungated warning is the one a release run sees; the debug line alone would be invisible there.
		qWarning() << "mascots_media:" << this->mediaSetting
			<< "is not one of Images, Videos or Mixed; showing images.";
	}

	if (!this->warnedUnsupportedMode && !modeValue.isEmpty() && !modeOk) {
		this->warnedUnsupportedMode = true;
		this->logVideo(QStringLiteral("mascots_mode=\"%1\" is not one of Static, Scroll or Stripes; using Static")
			.arg(modeValue));
		qWarning() << "mascots_mode:" << modeValue
			<< "is not one of Static, Scroll or Stripes; using Static.";
	}

	if (!this->warnedUnsupportedFill && !fillValue.isEmpty() && !fillOk) {
		this->warnedUnsupportedFill = true;
		this->logVideo(QStringLiteral("mascots_fill=\"%1\" is not one of Cover, Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit")
			.arg(fillValue));
		qWarning() << "mascots_fill:" << fillValue
			<< "is not one of Cover, Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit.";
	}

	if (!this->warnedUnsupportedStripesOrientation && !orientationValue.isEmpty() && !orientationOk) {
		this->warnedUnsupportedStripesOrientation = true;
		this->logVideo(QStringLiteral("mascots_stripes_orientation=\"%1\" is not one of Horizontal or Vertical; using Horizontal")
			.arg(orientationValue));
		qWarning() << "mascots_stripes_orientation:" << orientationValue
			<< "is not one of Horizontal or Vertical; using Horizontal.";
	}

	// The pan's own switch: off holds every band at its framing, whatever the speed row says. Kept as its own key so
	// "do not pan" is a control of its own rather than the speed's zero.
	this->columnsLayout.scrollSpeed = config->get_bool(QStringLiteral("mascots_scroll_enabled"))
		? clampedConfigInt(config, QStringLiteral("mascots_scroll_speed"), 40, 0, MAX_SCROLL_SPEED)
		: 0;
	this->scrollDirectionText = config->get(QStringLiteral("mascots_scroll_direction"));
	bool directionOk = false;
	const ScrollDirection parsedDirection =
		MascotController::scrollDirectionFromString(this->scrollDirectionText, &directionOk);
	this->columnsLayout.scrollDirection = directionOk ? parsedDirection : ScrollDirection::LeftToRight;
	this->columnsLayout.scrollPingpong = config->get_bool(QStringLiteral("mascots_scroll_pingpong"));

	if (!this->warnedUnsupportedScrollDirection && !this->scrollDirectionText.isEmpty() && !directionOk) {
		this->warnedUnsupportedScrollDirection = true;
		this->logScroll(QStringLiteral("mascots_scroll_direction=\"%1\" is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight")
			.arg(this->scrollDirectionText));
		qWarning() << "mascots_scroll_direction:" << this->scrollDirectionText
			<< "is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight.";
	}


	this->columnsEnabled = config->get_bool(QStringLiteral("mascots"));
	this->backgroundEnabled = config->get_bool(QStringLiteral("mascots_background"));
	bool backgroundModeOk = false;
	const QString backgroundModeValue = config->get(QStringLiteral("mascots_background_mode"));
	this->backgroundLayout.mode =
		MascotController::modeFromString(backgroundModeValue, &backgroundModeOk);
	bool backgroundFillOk = false;
	const QString backgroundFillValue = config->get(QStringLiteral("mascots_background_fill"));
	this->backgroundLayout.fill =
		MascotController::fillModeFromString(backgroundFillValue, &backgroundFillOk);
	this->backgroundStripes = clampedConfigInt(
		config, QStringLiteral("mascots_background_stripes"), 3, 1, 8);
	bool backgroundOrientationOk = false;
	const QString backgroundOrientationValue =
		config->get(QStringLiteral("mascots_background_stripes_orientation"));
	this->backgroundLayout.orientation = MascotController::stripesOrientationFromString(
		backgroundOrientationValue, &backgroundOrientationOk);
	this->backgroundLayout.alternate =
		config->get_bool(QStringLiteral("mascots_background_stripes_alternate"));
	this->backgroundLayout.scrollSpeed = clampedConfigInt(
		config, QStringLiteral("mascots_background_scroll_speed"), 50, 0, MAX_SCROLL_SPEED);
	bool backgroundScrollDirectionOk = false;
	const QString backgroundScrollDirectionValue =
		config->get(QStringLiteral("mascots_background_scroll_direction"));
	this->backgroundLayout.scrollDirection = MascotController::scrollDirectionFromString(
		backgroundScrollDirectionValue, &backgroundScrollDirectionOk);
	this->backgroundLayout.scrollPingpong =
		config->get_bool(QStringLiteral("mascots_background_scroll_pingpong"));
	if (!this->warnedUnsupportedBackgroundMode && !backgroundModeValue.isEmpty() && !backgroundModeOk) {
		this->warnedUnsupportedBackgroundMode = true;
		this->logBackground(QStringLiteral("mascots_background_mode=\"%1\" is not one of Static, Scroll or Stripes; using Static")
			.arg(backgroundModeValue));
		qWarning() << "mascots_background_mode:" << backgroundModeValue
			<< "is not one of Static, Scroll or Stripes; using Static.";
	}
	if (!this->warnedUnsupportedBackgroundFill && !backgroundFillValue.isEmpty() && !backgroundFillOk) {
		this->warnedUnsupportedBackgroundFill = true;
		this->logBackground(QStringLiteral("mascots_background_fill=\"%1\" is not one of Cover, Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit")
			.arg(backgroundFillValue));
		qWarning() << "mascots_background_fill:" << backgroundFillValue
			<< "is not one of Cover, Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit.";
	}
	if (!this->warnedUnsupportedBackgroundStripesOrientation
		&& !backgroundOrientationValue.isEmpty() && !backgroundOrientationOk) {
		this->warnedUnsupportedBackgroundStripesOrientation = true;
		this->logBackground(QStringLiteral("mascots_background_stripes_orientation=\"%1\" is not one of Horizontal or Vertical; using Horizontal")
			.arg(backgroundOrientationValue));
		qWarning() << "mascots_background_stripes_orientation:" << backgroundOrientationValue
			<< "is not one of Horizontal or Vertical; using Horizontal.";
	}
	if (!this->warnedUnsupportedBackgroundScrollDirection
		&& !backgroundScrollDirectionValue.isEmpty() && !backgroundScrollDirectionOk) {
		this->warnedUnsupportedBackgroundScrollDirection = true;
		this->logBackground(QStringLiteral("mascots_background_scroll_direction=\"%1\" is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight")
			.arg(backgroundScrollDirectionValue));
		qWarning() << "mascots_background_scroll_direction:" << backgroundScrollDirectionValue
			<< "is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight.";
	}


	this->source.setAllFilesRandom(config->get_bool("mascots_allfiles_random"));
	this->source.setWeightedChoice(this->weightedRandom);
	this->applySeedSource();
	// Before the panels are built, so the mode the log names is the mode the players are created with.
	this->applyDecoderBudget();
	// Before anything writes into a band: the mode decides how many bands a panel has.
	this->applyMode();
	// After applyMode(), which settled the band counts, and before ensureVideoSlots() creates the players.
	this->applyBandBudget();
	this->applyBackgroundBudget();
	// A pan switched off holds the offset it stopped at, which would leave the picture sitting off-centre for ever;
	// clearing it makes "speed 0" mean the framing the fill mode describes, centred.
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		if (this->layoutFor(panel).mode != MascotMode::Static && this->layoutFor(panel).scrollSpeed <= 0)
			this->resetScrollOffsets(panel);
	}
	this->syncSheet();
	if (this->backgroundState.surface) {
		this->syncBands(this->backgroundState, MascotPanel::Background);
		this->updateSheetVisibility();
	}

	this->pushVeilState();

	const bool mirror = this->mirrorEnabled();
	for (PanelState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		state->surface->setMirror(false);
		state->flipToggled = false;
		for (auto& owned : state->bands) {
			MascotBand* band = owned.get();
			if (!band || !band->video())
				continue;
			// `mascots_clip_seconds` also re-arms the clip timer: a slot left with the old length would pick its start
			// offset from a range that length no longer means (30 s down to 5 s leaves a 30 s range).
			band->video()->setRandomStart(this->randomStart, this->clipSeconds);
			band->video()->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
			band->video()->setLowLatency(this->lowLatency);
			// Discard a parked clip so "off" means off from the next swap, not one clip later.
			if (!this->preloadEnabled)
				band->video()->discardPreload();
		}
	}
	if (this->backgroundState.surface) {
		for (auto& owned : this->backgroundState.bands) {
			MascotBand* band = owned.get();
			if (!band || !band->video())
				continue;
			band->video()->setRandomStart(this->randomStart, this->clipSeconds);
			band->video()->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
			band->video()->setLowLatency(this->lowLatency);
			if (!this->preloadEnabled)
				band->video()->discardPreload();
		}
	}
	this->applyDisplay(this->leftState, MascotPanel::Left);
	this->applyDisplay(this->rightState, MascotPanel::Right);
	this->applyDisplay(this->backgroundState, MascotPanel::Background);
	// Turning Mirror on must make both columns show one media now: the parity repaint cannot change what the
	// right band holds, and a static configuration never advances. The first Apply records the value instead of filling.
	const bool mirrorNow = this->mirrorEnabled();
	if (this->mirrorAppliedKnown && mirrorNow != this->mirrorAppliedLast && this->running) {
		if (mirrorNow) {
			for (int i = 0; i < this->bandCount(MascotPanel::Left); ++i) {
				if (this->leftState.band(i) && this->rightState.band(i))
					this->nextBandImages(i, false);
			}
		}
		else {
			for (int i = 0; i < this->bandCount(MascotPanel::Right); ++i) {
				MascotBand* rightBand = this->rightState.band(i);
				if (!rightBand)
					continue;
				rightBand->playback().mirrorCopyPending = false;
				if (this->leftState.band(i))
					this->pickFor(this->rightState, MascotPanel::Right, i, false);
			}
		}
	}
	this->mirrorAppliedLast = mirrorNow;
	this->mirrorAppliedKnown = true;
	this->updateLayout();
	const bool downgraded = this->ensureVideoSlots();
	this->applyVideoPaused();
	this->updateScrollDriver();
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		if (!state.surface)
			continue;
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (band && band->media().type == MascotMedia::Video && !band->media().path.isEmpty())
				this->restartClipTimer(panel, i);
		}
	}
	// A panel that lost its players is drawing nothing and nothing else will fill it, so the Apply owes the pair one queued pick.
	if (downgraded && this->running) {
		if (this->columnsEnabled) {
			this->logVideo(QStringLiteral("panels downgraded from video to images; repainting them"));
			QMetaObject::invokeMethod(this, [this]() { this->nextImages(false); }, Qt::QueuedConnection);
		}
	}
	if (this->running && this->backgroundState.surface) {
		for (int i = 0; i < static_cast<int>(this->backgroundState.bands.size()); ++i) {
			MascotBand* band = this->backgroundState.band(i);
			if (!band || !band->media().path.isEmpty() || !band->requestPath().isEmpty())
				continue;
			QMetaObject::invokeMethod(this, [this, i]() {
				if (this->running && this->backgroundState.surface)
					this->pickFor(this->backgroundState, MascotPanel::Background, i, i == 0);
			}, Qt::QueuedConnection);
		}
	}
}

void MascotController::applySeedSource()
{
	if (!this->App || !this->App->config)
		return;
	Config* config = this->App->config;
	this->source.setSeedSource(config->get("random_seed"),
		config->get_bool("random_use_seed") && config->get_bool("mascots_use_seed"));
}

MascotMode MascotController::modeFromString(const QString& value, bool* ok)
{
	if (ok)
		*ok = true;
	if (value.compare(QStringLiteral("Static"), Qt::CaseInsensitive) == 0)
		return MascotMode::Static;
	if (value.compare(QStringLiteral("Scroll"), Qt::CaseInsensitive) == 0)
		return MascotMode::Scroll;
	if (value.compare(QStringLiteral("Stripes"), Qt::CaseInsensitive) == 0)
		return MascotMode::Stripes;
	if (ok)
		*ok = false;
	return MascotMode::Static;
}

StripesOrientation MascotController::stripesOrientationFromString(const QString& value, bool* ok)
{
	if (ok)
		*ok = true;
	if (value.compare(QStringLiteral("Horizontal"), Qt::CaseInsensitive) == 0)
		return StripesOrientation::Horizontal;
	if (value.compare(QStringLiteral("Vertical"), Qt::CaseInsensitive) == 0)
		return StripesOrientation::Vertical;
	if (ok)
		*ok = false;
	return StripesOrientation::Horizontal;
}

MascotSurface::FillMode MascotController::fillModeFromString(const QString& value, bool* ok)
{
	if (ok)
		*ok = true;
	if (value.compare(QStringLiteral("Crop"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::Crop;
	if (value.compare(QStringLiteral("Fit"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::Fit;
	if (value.compare(QStringLiteral("Stretch"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::Stretch;
	if (value.compare(QStringLiteral("HeightFit"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::HeightFit;
	if (value.compare(QStringLiteral("WidthFit"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::WidthFit;
	if (value.compare(QStringLiteral("Cover"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::Cover;
	if (value.compare(QStringLiteral("CoverScroll"), Qt::CaseInsensitive) == 0)
		return MascotSurface::FillMode::CoverScroll;
	if (ok)
		*ok = false;
	return MascotSurface::FillMode::HeightFit;
}

ScrollDirection MascotController::scrollDirectionFromString(const QString& value, bool* ok)
{
	if (ok)
		*ok = true;
	if (value.compare(QStringLiteral("LeftToRight"), Qt::CaseInsensitive) == 0)
		return ScrollDirection::LeftToRight;
	if (value.compare(QStringLiteral("RightToLeft"), Qt::CaseInsensitive) == 0)
		return ScrollDirection::RightToLeft;
	if (value.compare(QStringLiteral("TopToBottom"), Qt::CaseInsensitive) == 0)
		return ScrollDirection::TopToBottom;
	if (value.compare(QStringLiteral("BottomToTop"), Qt::CaseInsensitive) == 0)
		return ScrollDirection::BottomToTop;
	if (ok)
		*ok = false;
	return ScrollDirection::LeftToRight;
}

MascotSurface::FillMode MascotController::effectiveFillMode(MascotPanel panel) const
{
	// The chosen mode is the framing, in every mode of every panel. It used to be normalised for a panning band
	// (HeightFit horizontally, WidthFit vertically) because the QML used to apply those modes itself and only a
	// painted-box item can be slid; the surface now draws every mode as its painted box, so honouring the choice costs
	// nothing and the modes no longer collapse into one another.
	return this->layoutFor(panel).fill;
}

int MascotController::bandCount(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount();
	if (this->layoutFor(panel).mode != MascotMode::Stripes)
		return 1;
	return this->stripesCountFor(panel);
}

int MascotController::backgroundBandCount() const
{
	if (this->backgroundPlayers <= 0)
		return 0;
	if (this->backgroundLayout.mode == MascotMode::Stripes
		|| this->backgroundLayout.mode == MascotMode::Scroll)
		return std::min(this->backgroundStripes, this->backgroundPlayers);
	return 1;
}

bool MascotController::backgroundMarqueeActive(MascotPanel panel) const
{
	return panel == MascotPanel::Background
		&& this->layoutFor(panel).mode == MascotMode::Scroll
		&& this->bandCount(panel) >= 2;
}

bool MascotController::bandMirrorsLeft(int bandIndex) const
{
	return this->mirrorEnabled() && bandIndex >= 0 && bandIndex < this->bandCount(MascotPanel::Left);
}

QVector<QRectF> MascotController::bandRects(MascotPanel panel) const
{
	const PanelState& state = this->stateFor(panel);
	if (!state.surface)
		return QVector<QRectF>();
	const QRectF frame(this->panelFrame(panel));
	if (this->backgroundMarqueeActive(panel)) {
		const int count = this->bandCount(panel);
		const bool vertical = this->scrollDirectionIsVertical(panel);
		QVector<QRectF> rects;
		rects.reserve(count);
		for (int i = 0; i < count; ++i)
			rects.push_back(marqueeBandRect(frame, count, i, state.marqueeOffset, vertical));
		return rects;
	}
	return slicedBandRects(frame, this->bandCount(panel), this->stripesStackVertically(panel));
}

bool MascotController::stripesStackVertically(MascotPanel panel) const
{
	const PanelLayout& layout = this->layoutFor(panel);
	return layout.mode != MascotMode::Stripes
		|| layout.orientation == StripesOrientation::Horizontal;
}

void MascotController::applyMode()
{
	// Never from inside a wrap-point commit: applyMode() rebuilds the band vector, and a commit must not pull it
	// down under a tick. Re-queued instead, so the change still happens on the next turn with nothing iterating.
	if (this->wrapCommitDepth > 0) {
		QMetaObject::invokeMethod(this, [this]() { this->applyMode(); }, Qt::QueuedConnection);
		return;
	}
	if (!this->leftState.surface && !this->rightState.surface && !this->backgroundState.surface)
		return;
	const bool modeChanged = this->columnsLayout.mode != this->columnsLayout.appliedMode;
	bool countChanged = false;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		const PanelState& state = this->stateFor(panelOf(side));
		if (state.surface && state.configuredBandCount != this->bandCount(panelOf(side)))
			countChanged = true;
	}
	const bool orientationChanged = this->columnsLayout.applied
		&& this->columnsLayout.mode == MascotMode::Stripes
		&& this->columnsLayout.orientation != this->columnsLayout.appliedOrientation;
	const bool backgroundModeChanged = this->backgroundState.surface
		&& this->backgroundLayout.mode != this->backgroundLayout.appliedMode;
	const bool backgroundCountChanged = this->backgroundState.surface
		&& this->backgroundState.configuredBandCount != this->bandCount(MascotPanel::Background);
	const bool backgroundOrientationChanged = this->backgroundState.surface
		&& this->backgroundLayout.applied
		&& this->backgroundLayout.mode == MascotMode::Stripes
		&& this->backgroundLayout.orientation != this->backgroundLayout.appliedOrientation;
	if (this->columnsLayout.applied && !modeChanged && !countChanged && !orientationChanged
		&& !backgroundModeChanged && !backgroundCountChanged && !backgroundOrientationChanged)
		return;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		if (state.surface)
			this->syncBands(state, panelOf(side));
	}
	if (this->backgroundState.surface)
		this->syncBands(this->backgroundState, MascotPanel::Background);
	this->columnsLayout.appliedMode = this->columnsLayout.mode;
	this->columnsLayout.appliedOrientation = this->columnsLayout.orientation;
	this->columnsLayout.applied = true;
	this->backgroundLayout.appliedMode = this->backgroundLayout.mode;
	this->backgroundLayout.appliedOrientation = this->backgroundLayout.orientation;
	this->backgroundLayout.applied = true;
	if (modeChanged) {
		for (PanelState* state : { &this->leftState, &this->rightState }) {
			state->scrollPhase = 0.0;
			state->scrollZeroTravelLogged.fill(false);
		}
		this->resetScrollOffsets(MascotPanel::Left);
		this->resetScrollOffsets(MascotPanel::Right);
	}
	if (backgroundModeChanged) {
		this->backgroundState.scrollPhase = 0.0;
		this->backgroundState.scrollZeroTravelLogged.fill(false);
		this->resetScrollOffsets(MascotPanel::Background);
	}
	// An orientation change keeps the phase -- the fraction is axis-agnostic -- but clears offsets written on the old axis.
	if (orientationChanged) {
		for (PanelState* state : { &this->leftState, &this->rightState }) {
			MascotSurface* surface = state->surface.data();
			if (surface) {
				for (auto& owned : state->bands) {
					MascotBand* band = owned.get();
					if (!band || band->scrollOffset() == 0.0)
						continue;
					surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
					band->setScrollOffset(0.0);
				}
			}
			state->scrollZeroTravelLogged.fill(false);
		}
	}
	if (backgroundOrientationChanged) {
		MascotSurface* surface = this->backgroundState.surface.data();
		if (surface) {
			for (auto& owned : this->backgroundState.bands) {
				MascotBand* band = owned.get();
				if (!band || band->scrollOffset() == 0.0)
					continue;
				surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
				band->setScrollOffset(0.0);
			}
		}
		this->backgroundState.scrollZeroTravelLogged.fill(false);
	}
	if (countChanged || backgroundCountChanged)
		this->updateLayout();
	QMetaObject::invokeMethod(this, [this]() { this->fillEmptyBands(); }, Qt::QueuedConnection);
}

void MascotController::syncBands(PanelState& state, MascotPanel panel)
{
	MascotSurface* surface = state.surface.data();
	if (!surface)
		return;
	const int wanted = this->bandCount(panel);
	QStringList removedShown;
	while (static_cast<int>(state.bands.size()) > wanted) {
		removedShown.push_back(state.bands.back()->media().path);
		removedShown.push_back(state.bands.back()->playback().candidate.path);
		// A retired band that is not at the top of the sheet keeps its delegate, and a later slot paints over it:
		// emptying the slot and taking its rectangle is what actually retires it.
		state.bands.back()->clear();
		state.bands.back()->setRect(QRectF());
		state.bands.pop_back();
	}
	for (auto& owned : state.bands)
		owned->setSurface(surface);
	while (static_cast<int>(state.bands.size()) < wanted) {
		const int index = static_cast<int>(state.bands.size());
		auto created = std::make_unique<MascotBand>(state.slotBase + index);
		created->setSurface(surface);
		state.bands.push_back(std::move(created));
	}
	for (auto& owned : state.bands)
		owned->setGroup(this->slotGroupFor(panel));
	// A slot outlives the band that filled it, and the surface keeps the slot's scroll offset along with its other
	// properties. A band re-created into that slot starts at zero and only writes an offset when it has travel, so
	// the previous band's translation would sit on the item for ever and slide the new content out of its band.
	// Clearing it here is what makes a band-count change start the pan from the band rather than from the slot.
	for (auto& owned : state.bands) {
		MascotBand* band = owned.get();
		if (!band)
			continue;
		surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
		band->setScrollOffset(0.0);
	}
	state.configuredBandCount = wanted;
	state.scrollZeroTravelLogged.resize(wanted);
	for (const QString& path : removedShown)
		this->syncPin(path);
}

void MascotController::fillEmptyBands()
{
	if (!this->running)
		return;
	if (!this->columnsEnabled)
		return;
	// This fill refuses the background structurally: it is a fill with an image, and a portrait still across the window is the one look the layer must never draw.
	const bool mirror = this->mirrorEnabled();
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		if (!state.surface)
			continue;
		const int count = static_cast<int>(state.bands.size());
		for (int i = 0; i < count; ++i) {
			MascotBand* band = state.band(i);
			if (!band || !band->media().path.isEmpty())
				continue;
			if (mirror && side == MascotSide::Right && this->bandMirrorsLeft(i)) {
				MascotBand* leftBand = this->leftState.band(i);
				const bool mayOwn = this->bandMayOwnPlayer(panelOf(side), i);
				if (mayOwn && leftBand
					&& (leftBand->requestIsVideo() || leftBand->playback().videoPickRetryPending)) {
					band->playback().mirrorCopyPending = true;
					continue;
				}
				if (leftBand && !leftBand->media().path.isEmpty()) {
					if (leftBand->media().type == MascotMedia::Video) {
						if (mayOwn) {
							this->startMirrorVideo(i, leftBand->media().path,
								leftBand->media().startOffsetMs);
							continue;
						}
					}
					else {
						this->preparePick(state, i, false);
						this->showMedia(state, MascotPanel::Right, leftBand->media(), false, true, i);
						continue;
					}
				}
				if (leftBand && !leftBand->requestPath().isEmpty() && !leftBand->requestIsVideo())
					continue;
				if (mayOwn || !this->mediaSettingWantsVideo())
					continue;
			}
			if (!band->requestPath().isEmpty()) {
				if (band->requestIsVideo())
					continue;
				MascotPrefetcher* source = this->prefetcher.data();
				if (!source || source->isPending(band->requestPath()))
					continue;
			}
			if (i == 0) {
				if (mirror)
					this->nextBandImages(0, false);
				else
					this->pickFor(state, panelOf(side), i, true);
				continue;
			}
			this->pickFor(state, panelOf(side), i, false);
		}
	}
}

void MascotController::updateLayout()
{
	// The sheet is the window's rectangle and is in no layout: the host's own resize is what sizes it.
	if (this->sheet) {
		QWidget* host = this->backgroundHost.data();
		if (host && host->width() > 0 && host->height() > 0)
			this->sheet->setGeometry(host->rect());
	}
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		if (!state.surface)
			continue;
		const QVector<QRectF> rects = this->bandRects(panel);
		for (int i = 0; i < static_cast<int>(state.bands.size()) && i < rects.size(); ++i)
			state.bands[static_cast<std::size_t>(i)]->setRect(rects.at(i));
	}
	QWidget* window = nullptr;
	if (this->leftState.surface)
		window = this->leftState.surface->window();
	if (!window && this->rightState.surface)
		window = this->rightState.surface->window();
	if (!window && this->backgroundState.surface)
		window = this->backgroundState.surface->window();
	this->watchWindow(window);
}

double MascotController::scrollTravel(MascotPanel panel, const MascotBand& band) const
{
	MascotSurface* surface = band.surface();
	if (!surface)
		return 0.0;
	// `Cover`/`Crop` are the static framings: they fill the band and stay put, which is what separates them from
	// `CoverScroll`. `Fit`/`Stretch` fit the band and have no overflow to travel anyway.
	const MascotSurface::FillMode fill = this->effectiveFillMode(panel);
	if (fill == MascotSurface::FillMode::Cover || fill == MascotSurface::FillMode::Crop
		|| fill == MascotSurface::FillMode::Fit || fill == MascotSurface::FillMode::Stretch)
		return 0.0;
	const bool vertical = this->scrollDirectionIsVertical(panel);
	const QSizeF content = surface->slotContentSize(band.slotIndex());
	const QRectF rect = band.rect();
	const double contentExtent = vertical ? content.height() : content.width();
	const double bandExtent = vertical ? rect.height() : rect.width();
	const double travel = contentExtent - bandExtent;
	return travel > 0.0 ? travel : 0.0;
}



void MascotController::startScrollDriver()
{
	if (!this->scrollTimer) {
		this->scrollTimer = new QTimer(this);
		this->scrollTimer->setTimerType(Qt::PreciseTimer);
		this->scrollTimer->setInterval(SCROLL_TICK_MS);
		connect(this->scrollTimer, &QTimer::timeout, this, &MascotController::onScrollTick);
	}
	if (this->scrollTimer->isActive())
		return;
	// The clock arms with the driver, never before it: a delta across a pause would be spent as a jump on resume.
	this->scrollClock.start();
	this->scrollTimer->start();
}

void MascotController::stopScrollDriver()
{
	if (!this->scrollTimer || !this->scrollTimer->isActive())
		return;
	this->scrollTimer->stop();
	this->scrollClock.invalidate();
}

void MascotController::updateScrollDriver()
{
	if (this->scrollShouldRun())
		this->startScrollDriver();
	else
		this->stopScrollDriver();
}

bool MascotController::scrollShouldRun() const
{
	if (!this->running)
		return false;
	if (!this->panModeActive())
		return false;
	if (!this->panelsVisible())
		return false;
	return !this->videoShouldPause();
}

bool MascotController::scrollCanTick() const
{
	if (!this->running)
		return false;
	MascotSurface* surface = nullptr;
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelState& state = this->stateFor(panel);
		MascotSurface* candidate = state.surface.data();
		if (!candidate || this->layoutFor(panel).mode == MascotMode::Static)
			continue;
		if (!candidate->isVisible() || !windowOnScreen(candidate->window()))
			continue;
		surface = candidate;
		break;
	}
	if (!surface)
		return false;
	return !this->videoShouldPause();
}

bool MascotController::panelsVisible() const
{
	return this->anySurfaceVisible();
}

bool MascotController::anySurfaceVisible() const
{
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelState& state = this->stateFor(panel);
		MascotSurface* surface = state.surface.data();
		if (!surface || !surface->isVisible())
			continue;
		QWidget* window = surface->window();
		if (window && utils::IsMyWindowVisible(window))
			return true;
	}
	return false;
}

bool MascotController::panModeActive() const
{
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		MascotSurface* surface = this->stateFor(panel).surface.data();
		if (!surface || !surface->isVisible())
			continue;
		if (this->layoutFor(panel).mode != MascotMode::Static)
			return true;
	}
	return false;
}


bool MascotController::scrollDirectionReverses(MascotPanel panel) const
{
	const ScrollDirection direction = this->layoutFor(panel).scrollDirection;
	bool reverses = direction == ScrollDirection::RightToLeft
		|| direction == ScrollDirection::BottomToTop;
	if (panel == MascotPanel::Right && this->pairScrollOpposite)
		reverses = !reverses;
	return reverses;
}

bool MascotController::scrollDirectionIsVertical(MascotPanel panel) const
{
	const ScrollDirection direction = this->layoutFor(panel).scrollDirection;
	return direction == ScrollDirection::TopToBottom
		|| direction == ScrollDirection::BottomToTop;
}

double MascotController::scrollPhaseOffset(MascotPanel panel, const MascotBand& band) const
{
	// Under pingpong two staggered bands do coincide for an instant at the fold; that is inherent to a reflection,
	// and shortening the phase to dodge it was rejected because the fold is 2-to-1 whatever the offsets are.
	const PanelState& state = this->stateFor(panel);
	// The band's index inside its panel, not the sheet slot it addresses (those differ by the panel's base).
	const int localIndex = band.slotIndex() - state.slotBase;
	const int count = this->bandCount(panel);
	if (count <= 1 || localIndex < 0 || localIndex >= count || state.band(localIndex) != &band)
		return 0.0;
	return static_cast<double>(localIndex) / static_cast<double>(count);
}

bool MascotController::scrollReversed(MascotPanel panel, const MascotBand& band) const
{
	const PanelLayout& layout = this->layoutFor(panel);
	const int localIndex = band.slotIndex() - this->stateFor(panel).slotBase;
	return layout.mode == MascotMode::Stripes
		&& layout.alternate
		&& (localIndex % 2) != 0;
}

void MascotController::resetScrollOffsets(MascotPanel panel)
{
	PanelState& state = this->stateFor(panel);
	MascotSurface* surface = state.surface.data();
	if (surface) {
		for (auto& owned : state.bands) {
			MascotBand* band = owned.get();
			if (!band || band->scrollOffset() == 0.0)
				continue;
			surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
			band->setScrollOffset(0.0);
		}
	}
	state.scrollPhase = 0.0;
	state.marqueeOffset = 0.0;
	state.marqueePingpongSign = 1.0;
	state.marqueeLegDistance = 0.0;
}

void MascotController::onScrollTick()
{
	if (!this->scrollCanTick()) {
		this->stopScrollDriver();
		return;
	}
	if (!this->scrollClock.isValid()) {
		this->scrollClock.start();
		return;
	}
	const qint64 elapsedMs = this->scrollClock.restart();
	// A clock that went backwards (a suspended machine) must not run the pan in reverse.
	if (elapsedMs <= 0)
		return;
	const double dtSeconds = static_cast<double>(elapsedMs) / 1000.0;
	// A tick that advances the panel's phase by a whole travel makes every band alias, so those bands take the still branch below.
	std::vector<MascotBand::WrapCommit> wrapCommits;
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelLayout& layout = this->layoutFor(panel);
		if (layout.mode == MascotMode::Static)
			continue;
		const bool vertical = this->scrollDirectionIsVertical(panel);
		const bool reverse = this->scrollDirectionReverses(panel);
		const bool pingpong = layout.scrollPingpong;
		PanelState& state = this->stateFor(panel);
		MascotSurface* surface = state.surface.data();
		if (!surface || state.bands.empty())
			continue;
		if (panel == MascotPanel::Background && layout.mode == MascotMode::Scroll) {
			if (!this->backgroundMarqueeActive(panel))
				continue;
			const QRectF frame(this->panelFrame(panel));
			const double axisLen = vertical ? frame.height() : frame.width();
			const int count = static_cast<int>(state.bands.size());
			if (axisLen <= 0.0 || count < 2)
				continue;
			const double cellLen = axisLen / static_cast<double>(count - 1);
			const double total = static_cast<double>(count) * cellLen;
			const double speed = static_cast<double>(layout.scrollSpeed);
			double sign = reverse ? -1.0 : 1.0;
			if (pingpong)
				sign *= state.marqueePingpongSign;
			const double offsetBefore = state.marqueeOffset;
			const double offsetAfter = offsetBefore + sign * speed * dtSeconds;
			state.marqueeOffset = offsetAfter;
			if (pingpong && speed > 0.0) {
				state.marqueeLegDistance += std::abs(speed * dtSeconds);
				if (state.marqueeLegDistance >= total) {
					const double legs = std::floor(state.marqueeLegDistance / total);
					state.marqueeLegDistance = std::fmod(state.marqueeLegDistance, total);
					if (std::fmod(legs, 2.0) != 0.0)
						state.marqueePingpongSign = -state.marqueePingpongSign;
				}
			}
			for (int i = 0; i < count; ++i) {
				MascotBand* band = state.band(i);
				if (!band)
					continue;
				const QRectF rect = marqueeBandRect(frame, count, i, offsetAfter, vertical);
				if (rect != band->rect())
					band->setRect(rect);
				if (band->hasWrapCommit()) {
					const double rawBefore = static_cast<double>(i) * cellLen + offsetBefore;
					const double rawAfter = static_cast<double>(i) * cellLen + offsetAfter;
					const bool seam = std::floor(rawBefore / total) != std::floor(rawAfter / total);
					if (seam) {
						MascotBand::WrapCommit commit = band->takeWrapCommit();
						if (commit)
							wrapCommits.push_back(std::move(commit));
					}
				}
			}
			continue;
		}
		double referenceTravel = 0.0;
		for (const auto& owned : state.bands)
			referenceTravel = std::max(referenceTravel, this->scrollTravel(panel, *owned));
		const double phaseBefore = state.scrollPhase;
		double phaseRaw = phaseBefore;
		double phaseDelta = 0.0;
		if (referenceTravel > 0.0 && layout.scrollSpeed > 0) {
			const double delta = static_cast<double>(layout.scrollSpeed) * dtSeconds / referenceTravel;
			phaseDelta = delta;
			phaseRaw = phaseBefore + delta;
			state.scrollPhase = advancedScrollPhase(phaseBefore, delta);
		}
		const bool phaseAliases = referenceTravel > 0.0 && phaseDelta >= 1.0;
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			const double travel = this->scrollTravel(panel, *band);
			const bool travelAliases = travel > 0.0 && phaseAliases;
			if (travel <= 0.0 || travelAliases) {
				if (band->scrollOffset() != 0.0) {
					surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
					band->setScrollOffset(0.0);
				}
				if (!band->media().path.isEmpty() && !state.scrollZeroTravelLogged.value(i, false)) {
					state.scrollZeroTravelLogged[i] = true;
					if (travelAliases) {
						this->logScroll(QStringLiteral("%1 band %2 stands still: one %3ms tick advances the pass %4 times (its travel is %5px)")
							.arg(panelLabel(panel))
							.arg(i)
							.arg(elapsedMs)
							.arg(phaseDelta, 0, 'f', 2)
							.arg(travel, 0, 'f', 1));
					}
					else {
						const QSizeF content = surface->slotContentSize(band->slotIndex());
						this->logScroll(QStringLiteral("%1 band %2 stands still: its content is %3x%4 and its band is %5x%6")
							.arg(panelLabel(panel))
							.arg(i)
							.arg(content.width(), 0, 'f', 0)
							.arg(content.height(), 0, 'f', 0)
							.arg(band->rect().width(), 0, 'f', 0)
							.arg(band->rect().height(), 0, 'f', 0));
					}
				}
				continue;
			}
			const double offsetPhase = this->scrollPhaseOffset(panel, *band);
			const double fraction = scrollFractionFor(state.scrollPhase, offsetPhase, pingpong);
			const double offset = scrollOffsetFor(fraction, travel, reverse != this->scrollReversed(panel, *band));
			if (offset != band->scrollOffset()) {
				// The axis is the panel's for the same reason the travel is: `CoverScroll` follows the Direction, so
				// no band can pick an axis of its own from its picture.
				const bool vertical = this->scrollDirectionIsVertical(panel);
				surface->setSlotScrollOffset(band->slotIndex(),
					vertical ? 0.0 : offset, vertical ? offset : 0.0);
				band->setScrollOffset(offset);
			}
			if (band->hasWrapCommit()) {
				const bool seam = std::floor(phaseRaw + offsetPhase) != std::floor(phaseBefore + offsetPhase);
				if (seam) {
					MascotBand::WrapCommit commit = band->takeWrapCommit();
					if (commit)
						wrapCommits.push_back(std::move(commit));
				}
			}
		}
	}
	for (MascotBand::WrapCommit& commit : wrapCommits) {
		if (!this->running)
			break;
		++this->wrapCommitDepth;
		commit();
		--this->wrapCommitDepth;
	}
}

void MascotController::nextImages(bool forcedMirror)
{
	this->nextBandImages(0, forcedMirror);
}

void MascotController::nextBandImages(int bandIndex, bool forcedMirror)
{
	this->logVideo(QStringLiteral("MASCOT-ADVANCE pair band=%1 source=pair").arg(bandIndex));
	if (!this->running)
		return;
	const MascotMedia left = this->pickFor(this->leftState, MascotPanel::Left, bandIndex,
		bandIndex == 0);
	if (this->mirrorEnabled() || forcedMirror) {
		MascotBand* leftBand = this->leftState.band(bandIndex);
		if (!leftBand)
			return;
		if (!this->rightState.surface)
			return;
		MascotBand* rightBand = this->rightState.band(bandIndex);
		if (!rightBand)
			return;
		// A video's media does not exist until a frame reaches the sink, so the pair shares the file *and the start
		// offset*, which only the left load reports.
		if (leftBand->requestIsVideo() || leftBand->playback().videoPickRetryPending) {
			MascotVideoSlot* leftVideo = leftBand->video();
			if (leftVideo && leftVideo->isBusy()) {
				this->preparePick(this->rightState, bandIndex, false);
				rightBand->playback().mirrorCopyPending = true;
				return;
			}
			if (leftBand->playback().videoPickRetryPending) {
				this->preparePick(this->rightState, bandIndex, false);
				rightBand->playback().mirrorCopyPending = true;
				return;
			}
			if (leftBand->media().type == MascotMedia::Video && !leftBand->media().path.isEmpty()) {
				this->startMirrorVideo(bandIndex, leftBand->media().path,
					leftBand->media().startOffsetMs);
				return;
			}
			return;
		}
		if (!left.path.isEmpty()) {
			this->preparePick(this->rightState, bandIndex, false);
			this->showMedia(this->rightState, MascotPanel::Right, left, false, true, bandIndex);
			return;
		}
		if (leftBand->requestPath().isEmpty())
			return;
		this->preparePick(this->rightState, bandIndex, false);
		this->requestPath(this->rightState, leftBand->requestPath(), bandIndex);
	}
	else {
		this->pickFor(this->rightState, MascotPanel::Right, bandIndex, false);
	}
}

void MascotController::advanceBand(MascotSide side, int bandIndex)
{
	if (!this->running || bandIndex < 0)
		return;
	// A mirrored pair draws one clip per band, so a band advanced on either side is the pair's band: advancing half
	// of it would leave the two panels on different frames at the same height.
	if (this->mirrorEnabled()) {
		this->nextBandImages(bandIndex, false);
		return;
	}
	this->pickFor(this->stateFor(panelOf(side)), panelOf(side), bandIndex, bandIndex == 0);
}

void MascotController::advanceStrip(MascotSide side)
{
	if (!this->running)
		return;
	const int count = this->bandCount(panelOf(side));
	for (int i = 0; i < count; ++i)
		this->advanceBand(side, i);
}

void MascotController::flipSide(MascotSide side)
{
	PanelState& state = this->stateFor(panelOf(side));
	MascotBand* band = state.band(0);
	if (!state.surface || !band || band->media().path.isEmpty())
		return;
	state.flipToggled = !state.flipToggled;
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
		MascotBand* flipped = state.band(i);
		if (!flipped || flipped->media().path.isEmpty())
			continue;
		this->applyMirror(state, panelOf(side), flipped->media().path, i);
	}
}

void MascotController::flipBoth()
{
	this->flipSide(MascotSide::Left);
	this->flipSide(MascotSide::Right);
}

bool MascotController::flipForSide(const QString& path, MascotSide side) const
{
	const QString parent = utils::getParentDirectoryName(path, 1);
	if (side == MascotSide::Left)
		return parent == QStringLiteral("right");
	return parent == QStringLiteral("left");
}

color_area MascotController::colorFor(MascotSide side, bool weighted)
{
	return this->colorForBand(side, 0, weighted);
}

color_area MascotController::colorForBand(MascotSide side, int bandIndex, bool weighted)
{
	const PanelState& state = this->stateFor(panelOf(side));
	const MascotBand* band = state.band(bandIndex);
	color_area color;
	if (!band)
		return color;
	const MascotMedia& media = band->media();
	if (weighted) {
		if (!media.acceptedColors.isEmpty()) {
			QList<color_area> accepted = media.acceptedColors;
			color = utils::get_weighted_random_color(accepted);
		}
		else if (!media.rejectedColors.isEmpty()) {
			QList<color_area> rejected = media.rejectedColors;
			color = *utils::select_randomly(rejected.begin(), rejected.end());
		}
	}
	else {
		for (const color_area& candidate : media.acceptedColors) {
			if (candidate.area_percent > color.area_percent)
				color = candidate;
		}
		for (const color_area& candidate : media.rejectedColors) {
			if (candidate.area_percent > color.area_percent)
				color = candidate;
		}
	}
	return color;
}

void MascotController::showDroppedImage(MascotSide side, const QString& path)
{
	if (!this->running || path.isEmpty())
		return;
	if (!this->App || !this->App->debug_mode)
		return;
	PanelState& state = this->stateFor(panelOf(side));
	if (!state.surface)
		return;
	state.mirrorOverrideEnabled = true;
	state.mirrorOverrideValue = false;
	state.flipToggled = false;
	state.highlightOnApply = true;
	this->requestPath(state, path, 0);

	if (!this->mirrorEnabled())
		return;
	PanelState& otherState = this->stateFor(panelOf(other(side)));
	if (!otherState.surface)
		return;
	otherState.mirrorOverrideEnabled = true;
	otherState.mirrorOverrideValue = true;
	otherState.flipToggled = false;
	otherState.highlightOnApply = false;
	this->requestPath(otherState, path, 0);
}

bool MascotController::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
		// The sheet is in no layout, so nothing sizes it but updateLayout(), from the window-sized host -- final
		// only when that host's resize arrives. All three hosts are watched and a move counts: a band's rectangle
		// is its host's position and size, and the layout sets both after the central widget resized.
		if (!this->sheet)
			return QObject::eventFilter(watched, event);
		if (watched == this->leftHost.data() || watched == this->rightHost.data()
			|| watched == this->backgroundHost.data()) {
			this->updateLayout();
		}
	}
	if (this->watchedWindow && watched == this->watchedWindow.data()) {
		switch (event->type()) {
		case QEvent::WindowActivate:
			this->refreshWindowSeen();
			this->updateScrollDriver();
			break;
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::WindowDeactivate:
		case QEvent::WindowStateChange:
			this->refreshWindowSeen();
			this->updateScrollDriver();
			break;
		default:
			break;
		}
	}
	return QObject::eventFilter(watched, event);
}

MascotController::PanelState& MascotController::stateFor(MascotPanel panel)
{
	if (panel == MascotPanel::Background)
		return this->backgroundState;
	return panel == MascotPanel::Right ? this->rightState : this->leftState;
}

const MascotController::PanelState& MascotController::stateFor(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundState;
	return panel == MascotPanel::Right ? this->rightState : this->leftState;
}

MascotPanel MascotController::panelOf(MascotSide side)
{
	return side == MascotSide::Left ? MascotPanel::Left : MascotPanel::Right;
}

MascotController::PanelLayout& MascotController::layoutFor(MascotPanel panel)
{
	return panel == MascotPanel::Background ? this->backgroundLayout : this->columnsLayout;
}

const MascotController::PanelLayout& MascotController::layoutFor(MascotPanel panel) const
{
	return panel == MascotPanel::Background ? this->backgroundLayout : this->columnsLayout;
}

int MascotController::stripesCountFor(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundStripes;
	return panel == MascotPanel::Right ? this->stripesRightBands : this->stripesLeftBands;
}

bool MascotController::pairSideOf(const PanelState& state, MascotSide* side) const
{
	for (MascotSide candidate : { MascotSide::Left, MascotSide::Right }) {
		if (&this->stateFor(panelOf(candidate)) == &state) {
			if (side)
				*side = candidate;
			return true;
		}
	}
	return false;
}

MascotBand* MascotController::PanelState::band(int i) const
{
	if (i < 0 || i >= static_cast<int>(this->bands.size()))
		return nullptr;
	return this->bands[static_cast<std::size_t>(i)].get();
}

MascotSide MascotController::other(MascotSide side)
{
	return side == MascotSide::Left ? MascotSide::Right : MascotSide::Left;
}

bool MascotController::mirrorEnabled() const
{
	return this->App && this->App->config && this->App->config->get_bool("mascots_mirror");
}

bool MascotController::centerContentEnabled() const
{
	if (!this->App || !this->App->config)
		return true;
	return this->App->config->get_bool("mascots_center_content");
}

void MascotController::createSheet()
{
	if (!this->cache || this->sheet)
		return;
	QWidget* host = this->backgroundHost.data();
	if (!host)
		return;
	// One surface for the three layers, window-sized and in no layout; it is parented to the widget that used to hold the layer alone.
	// `alwaysStackOnTop` is false, which is what lets the sheet sit under the UI panes; the attribute would draw it above every sibling.
	// What that costs is an opaque blit, which is why the sheet always paints a backdrop -- the window's colour -- in every uncovered pixel.
	auto* surface = new MascotSurface(this->cache, host, false);
	this->sheet = surface;
	// The only place the slot count is set: the sheet holds every slot it can ever address from here on.
	surface->setSlotCount(SLOT_CAPACITY);
	this->leftState.slotBase = LEFT_SLOT_BASE;
	this->rightState.slotBase = RIGHT_SLOT_BASE;
	this->backgroundState.slotBase = BACKGROUND_SLOT_BASE;
	// Connected once for the sheet's life: a panel switched off and on again must not connect the same signal twice, or one release emits two clicks.
	connect(surface, &MascotSurface::clicked, this,
		[this](int slot, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
			MascotPanel panel = MascotPanel::Left;
			int band = -1;
			if (!this->bandForSlot(slot, &panel, &band))
				return;
			MascotSide side = MascotSide::Left;
			if (!this->pairSideOf(this->stateFor(panel), &side))
				return;
			emit this->clicked(side, band, button, modifiers);
		});
	connect(surface, &MascotSurface::imageDropped, this, [this](int slot, QString path) {
		MascotPanel panel = MascotPanel::Left;
		int band = -1;
		if (!this->bandForSlot(slot, &panel, &band))
			return;
		MascotSide side = MascotSide::Left;
		if (!this->pairSideOf(this->stateFor(panel), &side))
			return;
		this->showDroppedImage(side, path);
	});
	// The hosts sit above the sheet in the widget stack, so making them mouse-transparent is what routes clicks and drops to the sheet.
	for (QWidget* columnHost : { this->leftHost.data(), this->rightHost.data() }) {
		if (columnHost)
			columnHost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	}
	surface->setMirror(false);
	if (this->columnsEnabled) {
		this->attachPanel(this->leftState, MascotPanel::Left);
		this->attachPanel(this->rightState, MascotPanel::Right);
	}
	if (this->backgroundEnabled)
		this->attachPanel(this->backgroundState, MascotPanel::Background);
	for (QWidget* watched : { this->leftHost.data(), this->rightHost.data(), host })
		if (watched)
			watched->installEventFilter(this);
	// Geometry, stacking order and visibility, in that order: lower() puts the sheet beneath the layout's widgets,
	// and a child created after its parent was shown starts hidden.
	surface->setGeometry(host->rect());
	surface->lower();
	this->updateSheetVisibility();
	// The sheet must *not* be mouse-transparent: the columns receive their clicks and drops through it.
	this->backgroundLayout.appliedMode = this->backgroundLayout.mode;
	this->backgroundLayout.appliedOrientation = this->backgroundLayout.orientation;
	this->backgroundLayout.applied = true;
	this->columnsLayout.appliedMode = this->columnsLayout.mode;
	this->columnsLayout.appliedOrientation = this->columnsLayout.orientation;
	this->columnsLayout.applied = true;
	this->pushVeilState();
	this->updateLayout();
}

void MascotController::syncSheet()
{
	const bool wanted = this->running && (this->columnsEnabled || this->backgroundEnabled);
	if (!wanted) {
		if (this->sheet)
			this->destroySheet();
		return;
	}
	if (!this->sheet) {
		this->createSheet();
		return;
	}
	if (this->columnsEnabled) {
		if (!this->leftState.surface)
			this->attachPanel(this->leftState, MascotPanel::Left);
		if (!this->rightState.surface)
			this->attachPanel(this->rightState, MascotPanel::Right);
	}
	else {
		if (this->leftState.surface)
			this->dropPanel(this->leftState);
		if (this->rightState.surface)
			this->dropPanel(this->rightState);
	}
	if (this->backgroundEnabled) {
		if (!this->backgroundState.surface)
			this->attachPanel(this->backgroundState, MascotPanel::Background);
	}
	else if (this->backgroundState.surface) {
		this->dropPanel(this->backgroundState);
	}
	this->updateSheetVisibility();
}

void MascotController::destroySheet()
{
	if (!this->sheet)
		return;
	for (QWidget* watched : { this->leftHost.data(), this->rightHost.data(), this->backgroundHost.data() })
		if (watched)
			watched->removeEventFilter(this);
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		if (state.surface)
			this->dropPanel(state);
	}
	// Synchronous, not deleteLater(): a later start() must be able to build into the same host, and nothing calls stop() from the sheet's own handler.
	delete this->sheet.data();
	this->sheet = nullptr;
}

void MascotController::attachPanel(PanelState& state, MascotPanel panel)
{
	if (!this->sheet)
		return;
	state.surface = this->sheet;
	this->syncBands(state, panel);
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i)
		this->applyMirror(state, panel, state.bands[static_cast<std::size_t>(i)]->media().path, i);
	this->ensureVideoSlots();
}

void MascotController::dropPanel(PanelState& state)
{
	for (auto& owned : state.bands) {
		if (owned->video())
			owned->video()->shutdown();
	}
	this->clearSide(state);
	this->detachPanel(state);
}

void MascotController::detachPanel(PanelState& state)
{
	if (!state.surface)
		return;
	// The two caches the band keeps describe the scene's slot, so they die with the binding: a rebuilt scene starts
	// at MascotSurface's defaults. A surviving "the slot already carries HeightFit" would suppress the write forever after.
	for (auto& owned : state.bands)
		owned->setSurface(nullptr);
	this->destroyVideoSlot(state);
	state.surface = nullptr;
}

bool MascotController::bandForSlot(int slot, MascotPanel* panel, int* bandIndex) const
{
	for (MascotPanel candidate : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelState& state = this->stateFor(candidate);
		if (!state.surface)
			continue;
		const int band = slot - state.slotBase;
		if (band < 0 || band >= static_cast<int>(state.bands.size()))
			continue;
		if (panel)
			*panel = candidate;
		if (bandIndex)
			*bandIndex = band;
		return true;
	}
	return false;
}

QRectF MascotController::panelFrame(MascotPanel panel) const
{
	QWidget* host = nullptr;
	switch (panel) {
	case MascotPanel::Left:
		host = this->leftHost.data();
		break;
	case MascotPanel::Right:
		host = this->rightHost.data();
		break;
	case MascotPanel::Background:
		host = this->backgroundHost.data();
		break;
	}
	if (!host)
		return QRectF();
	if (panel == MascotPanel::Background)
		return QRectF(QPointF(0.0, 0.0), QSizeF(host->size()));
	return QRectF(QPointF(host->pos()), QSizeF(host->size()));
}

MascotSurface::SlotGroup MascotController::slotGroupFor(MascotPanel panel) const
{
	return panel == MascotPanel::Background ? MascotSurface::SlotGroup::Layer
		: MascotSurface::SlotGroup::Columns;
}

bool MascotController::mascotsBelowVeil() const
{
	if (!this->App || !this->App->config)
		return false;
	const QString value = this->App->config->get(QStringLiteral("mascots_columns_z"));
	return value.compare(QStringLiteral("Below"), Qt::CaseInsensitive) == 0
		|| value.compare(QStringLiteral("Below the veil"), Qt::CaseInsensitive) == 0;
}

void MascotController::updateSheetVisibility()
{
	if (!this->sheet)
		return;
	const bool wanted = this->leftState.surface || this->rightState.surface
		|| this->backgroundState.surface;
	if (this->sheet->isVisible() != wanted)
		this->sheet->setVisible(wanted);
}

void MascotController::pushVeil()
{
	this->pushVeilState();
}

void MascotController::pushVeilState()
{
	if (!this->sheet || !this->App)
		return;
	const QColor windowColour = this->App->palette().color(QPalette::Window);
	QColor backdrop = windowColour;
	double layerOpacity = 1.0;
	// `MM_MASCOT_FLAT=#RRGGBB` replaces the layer with a flat colour so the veil's blend can be measured against a known value.
	// It colours the *layer*, never the veil: a veil painted in the probe's colour would have nothing to blend.
	const QColor flat = QColor::fromString(qEnvironmentVariable("MM_MASCOT_FLAT"));
	if (flat.isValid()) {
		backdrop = flat;
		layerOpacity = 0.0;
	}
	this->sheet->setBackdrop(backdrop, true);
	this->sheet->setSurfaceOpacity(layerOpacity);
	double veilOpacity = 0.0;
	if (this->App->config) {
		veilOpacity = std::clamp(
			this->App->config->get_double(QStringLiteral("mascots_background_opacity"), 0.0), 0.0, 1.0);
	}
	const bool veilEnabled = this->backgroundEnabled;
	const bool aboveColumns = !this->mascotsBelowVeil();
	this->sheet->setVeil(windowColour, veilOpacity, veilEnabled, aboveColumns);
	if (this->App->debug_mode) {
		qDebug() << "MASCOT-VEIL" << "alpha" << qRound(veilOpacity * 255.0)
			<< "raw" << (this->App->config ? this->App->config->get("mascots_background_opacity") : QStringLiteral("<no config>"))
			<< "enabled" << veilEnabled
			<< "z" << (aboveColumns ? 1 : 3);
	}
}

void MascotController::clearSide(PanelState& state)
{
	QStringList previousShown;
	for (auto& owned : state.bands)
		previousShown.push_back(owned->media().path);
	QStringList previousCandidates;
	for (auto& owned : state.bands)
		previousCandidates.push_back(owned->playback().candidate.path);
	for (auto& owned : state.bands) {
		if (QTimer* timer = owned->clipTimer())
			timer->stop();
		if (owned->video())
			owned->video()->stopAll();
	}
	for (auto& owned : state.bands)
		owned->clear();
	state.flipToggled = false;
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.highlightOnApply = false;
	for (const QString& path : previousShown)
		this->syncPin(path);
	for (const QString& path : previousCandidates)
		this->syncPin(path);
}

bool MascotController::pinReferenced(const QString& path) const
{
	if (path.isEmpty())
		return false;
	for (const PanelState* state : { &this->leftState, &this->rightState }) {
		for (const auto& owned : state->bands) {
			if (owned->media().path == path)
				return true;
			if (owned->playback().candidate.path == path)
				return true;
		}
	}
	return false;
}

void MascotController::syncPin(const QString& path)
{
	if (path.isEmpty())
		return;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->pin(path, this->pinReferenced(path));
}

void MascotController::applyDisplay(PanelState& state, MascotPanel panel)
{
	if (!state.surface)
		return;
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
		MascotBand* band = state.bands[static_cast<std::size_t>(i)].get();
		if (!band)
			continue;
		const MascotMedia media = band->media();
		if (media.path.isEmpty())
			continue;
		this->presentMedia(state, panel, media, i);
	}
}

void MascotController::presentMedia(PanelState& state, MascotPanel panel, const MascotMedia& media,
	int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band)
		return;
	this->applyMirror(state, panel, media.path, bandIndex);
	band->applyFill(this->effectiveFillMode(panel));
	band->setMedia(media);
	this->logVideo(QStringLiteral("MASCOT-COMMIT %1 band=%2 kind=%3 file=%4 offset=%5")
		.arg(panelLabel(panel))
		.arg(bandIndex)
		.arg(media.type == MascotMedia::Video ? QStringLiteral("video") : QStringLiteral("image"))
		.arg(QFileInfo(media.path).fileName())
		.arg(media.startOffsetMs));
	if (media.type == MascotMedia::Video) {
		band->setContentOffset(QPointF(0.0, 0.0));
		band->setVideoOutputVisible(true);
		return;
	}
	if (band->videoOutputVisible()) {
		band->setVideoOutputVisible(false);
		if (band->video())
			band->video()->stopAll();
		// Hiding the outputs leaves their opacity where the last clip left it, so the next clip would load at the previous
		// clip's opacity and reveal the old frozen frame. Reset it here, where nothing of it can be seen, with duration 0: a reset, not a fade.
		band->setVideoOpacity(0, 0.0, 0);
		band->setVideoOpacity(1, 0.0, 0);
	}
	if (QTimer* timer = band->clipTimer())
		timer->stop();
	if (!this->centerContentEnabled())
		band->setContentOffset(QPointF(0.0, 0.0));
}

bool MascotController::mirrorParityFor(MascotPanel panel, const QString& path) const
{
	if (panel == MascotPanel::Left)
		return this->flipForSide(path, MascotSide::Left);
	if (panel == MascotPanel::Right)
		return this->flipForSide(path, MascotSide::Right);
	return false;
}

void MascotController::applyMirror(PanelState& state, MascotPanel panel, const QString& path,
	int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band)
		return;
	bool mirrored;
	// Parity has two sources, composed. The folder rule flips a file authored for one side landing in the other slot.
	// Mirror adds the pair rule: the folder rule decides which way the pair faces (through the left slot) and
	// `mascots_pair_flip` decides the pair's parity, so the look is the same for every media type.
	// XOR was measured wrong: a side-tagged image came out identical while a video came out mirrored.
	if (state.mirrorOverrideEnabled) {
		mirrored = state.mirrorOverrideValue;
	}
	else if (this->mirrorEnabled() && panel == MascotPanel::Right) {
		const bool leftParity = !path.isEmpty() && this->flipForSide(path, MascotSide::Left);
		mirrored = leftParity != this->flipMirroredColumn;
	}
	else {
		mirrored = !path.isEmpty()
			&& this->flipForSide(path, panel == MascotPanel::Left ? MascotSide::Left : MascotSide::Right);
	}
	if (state.flipToggled)
		mirrored = !mirrored;
	band->applyMirror(mirrored);
}

void MascotController::requestPath(PanelState& state, const QString& path, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	band->setRequestPath(path);
	band->setRequestIsVideo(false);
	band->playback().mirrorCopyPending = false;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

MascotMedia MascotController::pickFor(PanelState& state, MascotPanel panel, int bandIndex,
	bool updateHighlight)
{
	this->logVideo(QStringLiteral("MASCOT-PICK %1 band=%2").arg(panelLabel(panel)).arg(bandIndex));
	if (!this->running || !state.surface)
		return MascotMedia();
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return MascotMedia();

	const MascotMedia::Type type = this->pickType(state, panel);
	if (type == MascotMedia::Video) {
		switch (this->pickVideo(state, panel, bandIndex, updateHighlight)) {
		case VideoPick::Started:
			return MascotMedia();
		case VideoPick::Skip:
			return MascotMedia();
		case VideoPick::Fallback:
			if (panel == MascotPanel::Background) {
				if (!this->backgroundEmptyPoolLogged) {
					this->backgroundEmptyPoolLogged = true;
					this->logBackground(QStringLiteral("the video pool is empty; the background has no clip to show and stays blank"));
				}
				return MascotMedia();
			}
			break;
		}
	}
	if (this->mediaMayBeVideo(panel) && this->App && this->App->debug_mode) {
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastTypeLogMs == 0 || (now - state.lastTypeLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastTypeLogMs = now;
			this->logVideo(QStringLiteral("%1 pick=image pool=%2 media=%3")
				.arg(bandLabel(panel, bandIndex))
				.arg(this->source.videoPoolSize())
				.arg(this->mediaSetting));
		}
	}

	if (!band->playback().candidate.path.isEmpty()) {
		const MascotMedia media = band->playback().candidate;
		this->preparePick(state, bandIndex, updateHighlight);
		this->showMedia(state, panel, media, true, true, bandIndex);
		return media;
	}

	if (!band->requestPath().isEmpty()) {
		MascotPrefetcher* source = this->prefetcher.data();
		if (!source || source->isPending(band->requestPath()))
			return MascotMedia();
	}

	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return MascotMedia();

	this->preparePick(state, bandIndex, updateHighlight);
	this->requestPath(state, path, bandIndex);
	return MascotMedia();
}

QString MascotController::nextSourcePath()
{
	this->applySeedSource();
	return this->source.nextImagePath();
}

void MascotController::preparePick(PanelState& state, int bandIndex, bool updateHighlight)
{
	if (bandIndex != 0)
		return;
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.flipToggled = false;
	state.highlightOnApply = updateHighlight;
}

void MascotController::showMedia(PanelState& state, MascotPanel panel, const MascotMedia& media,
	bool fromReady, bool armCandidate, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || media.path.isEmpty())
		return;
	const bool policyBand = bandIndex == 0;

	const QString previousShown = band->media().path;
	const QString previousCandidate = band->playback().candidate.path;
	if (fromReady)
		band->playback().candidate = MascotMedia(); // consumed by this commit
	band->playback().committedAtMs = QDateTime::currentMSecsSinceEpoch();
	band->recordMedia(media);
	if (policyBand)
		state.flipToggled = false;
	band->setRequestPath(QString());
	band->setRequestIsVideo(false);
	band->playback().prefetchPath.clear();
	this->syncPin(media.path);
	this->presentMedia(state, panel, media, bandIndex);
	this->syncPin(previousShown);
	this->syncPin(previousCandidate);
	band->playback().videoFailures = 0;
	band->playback().imageFailures = 0;

	if (armCandidate)
		this->armSpeculation(panel, bandIndex);

	if (panel == MascotPanel::Left && media.type == MascotMedia::Image) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = false;
	}

	if (policyBand && state.highlightOnApply) {
		state.highlightOnApply = false;
		MascotSide pairSide = MascotSide::Left;
		if (this->pairSideOf(state, &pairSide))
			emit this->themeColorChanged(this->colorFor(pairSide, true));
	}

	if (this->mirrorEnabled() && panel == MascotPanel::Left && bandIndex > 0
		&& media.type == MascotMedia::Image) {
		PanelState& right = this->rightState;
		MascotBand* rightBand = right.band(bandIndex);
		if (right.surface && rightBand) {
			this->preparePick(right, bandIndex, false);
			this->showMedia(right, MascotPanel::Right, media, false, true, bandIndex);
		}
	}
}

void MascotController::holdCandidate(MascotBand& band, const MascotMedia& media)
{
	if (media.path.isEmpty())
		return;
	const QString previous = band.playback().candidate.path;
	band.playback().candidate = media;
	this->syncPin(media.path);
	this->syncPin(previous);
}

void MascotController::armSpeculation(MascotPanel panel, int bandIndex)
{
	QMetaObject::invokeMethod(this, [this, panel, bandIndex] {
		this->speculate(this->stateFor(panel), panel, bandIndex);
	}, Qt::QueuedConnection);
}

void MascotController::speculate(PanelState& state, MascotPanel panel, int bandIndex)
{
	if (!this->running || !state.surface)
		return;
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	if (!band->playback().candidate.path.isEmpty() || !band->playback().prefetchPath.isEmpty())
		return;
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
		return;
	if (this->pickType(state, panel) == MascotMedia::Video)
		return;
	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return;
	band->playback().prefetchPath = path;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}


bool MascotController::mediaSettingWantsVideo() const
{
	return this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0
		|| this->mediaSetting.compare(QStringLiteral("Mixed"), Qt::CaseInsensitive) == 0;
}

bool MascotController::mediaMayBeVideo(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount() > 0;
	if (!this->mediaSettingWantsVideo())
		return false;
	return this->videoBandsFor(MascotPanel::Left) > 0
		|| this->videoBandsFor(MascotPanel::Right) > 0;
}

int MascotController::videoBandsFor(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount();
	if (!this->columnsEnabled)
		return 0;
	if (!this->mediaSettingWantsVideo())
		return 0;
	if (this->layoutFor(panel).mode == MascotMode::Static)
		return this->videoMode != VideoMode::Off ? 1 : 0;
	return panel == MascotPanel::Left ? this->leftBandPlayers : this->rightBandPlayers;
}

bool MascotController::bandMayOwnPlayer(MascotPanel panel, int bandIndex) const
{
	return bandIndex >= 0
		&& bandIndex < this->bandCount(panel)
		&& bandIndex < this->videoBandsFor(panel);
}

bool MascotController::videoCrossfades(MascotPanel panel) const
{
	if (panel == MascotPanel::Background)
		return this->backgroundCrossfades;
	// A panning band crossfades the same way a static one does -- two players taking turns -- so the only question is
	// whether the decoder ceiling can pay for the pairs, which applyBandBudget() answers.
	return this->videoMode == VideoMode::Crossfade;
}

void MascotController::invalidateVideoPoolIfStale()
{
	MainWindow* window = this->App ? this->App->mainWindow : nullptr;
	if (!window)
		return;
	const QString db = this->App ? this->App->currentDB : QString();
	const int rows = window->videosModel ? window->videosModel->rowCount() : -1;
	if (this->poolFingerprintValid && this->poolDb == db && this->poolRowCount == rows
		&& this->poolFilterJson == window->filterSettings.json) {
		return;
	}
	this->poolFingerprintValid = true;
	this->poolDb = db;
	this->poolRowCount = rows;
	this->poolFilterJson = window->filterSettings.json;
	this->source.invalidateVideoPool();
}

MascotMedia::Type MascotController::pickType(PanelState& state, MascotPanel panel)
{
	if (panel == MascotPanel::Background) {
		this->invalidateVideoPoolIfStale();
		return MascotMedia::Video;
	}
	if (!this->mediaMayBeVideo(panel))
		return MascotMedia::Image;
	this->invalidateVideoPoolIfStale();
	const int pool = this->source.videoPoolSize();
	if (this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0 && pool <= 0) {
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastTypeLogMs == 0 || (now - state.lastTypeLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastTypeLogMs = now;
			this->logVideo(QStringLiteral("video pool is empty (0 candidates); showing images for now"));
		}
		return MascotMedia::Image;
	}
	return pool > 0 ? MascotMedia::Video : MascotMedia::Image;
}

MascotController::VideoPick MascotController::pickVideo(PanelState& state, MascotPanel panel,
	int bandIndex, bool updateHighlight)
{
	MascotBand* band = state.band(bandIndex);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!slot)
		return VideoPick::Fallback;
	if (slot->isBusy() && !slot->preloading())
		return VideoPick::Skip;
	// A deferral, not a skip: the clip timer that asked has already fired, so returning would leave the panel
	// on its old clip until the beat. The pick is re-asked until the tooltip stops.
	if (this->previewTooltipBusy()) {
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastPreviewDeferLogMs == 0
			|| (now - state.lastPreviewDeferLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastPreviewDeferLogMs = now;
			this->logVideo(QStringLiteral("%1 swap waits: a preview tooltip is playing")
				.arg(bandLabel(panel, bandIndex)));
		}
		band->playback().videoPickRetryPending = true;
		QTimer::singleShot(this->busyDeferMs, this, [this, panel, bandIndex]() {
			if (!this->running)
				return;
			PanelState& panelState = this->stateFor(panel);
			MascotBand* panelBand = panelState.band(bandIndex);
			MascotVideoSlot* panelVideo = panelBand ? panelBand->video() : nullptr;
			if (!panelBand || !panelVideo || panelBand->requestIsVideo()
				|| (panelVideo->isBusy() && !panelVideo->preloading()))
				return;
			this->pickFor(panelState, panel, bandIndex, bandIndex == 0);
		});
		return VideoPick::Skip;
	}
	// The sink belongs to the delegate and does not exist until QML has created it; a load started without one would
	// fail against a live panel that is a moment behind, so the pick is retried rather than reported.
	MascotSurface* surface = state.surface.data();
	if (!surface || !band || !surface->slotSink(band->slotIndex(), 0)) {
		if (this->App && this->App->debug_mode) {
			if (!this->warnedVideoSink) {
				this->warnedVideoSink = true;
				this->logVideo(QStringLiteral("no video sink for the panel yet; retrying the pick"));
			}
			band->playback().videoPickRetryPending = true;
			QTimer::singleShot(this->retryIntervalMs, this, [this, panel, bandIndex]() {
				if (!this->running)
					return;
				PanelState& panelState = this->stateFor(panel);
				MascotBand* panelBand = panelState.band(bandIndex);
				if (panelBand && panelBand->media().path.isEmpty())
					this->pickFor(panelState, panel, bandIndex, bandIndex == 0);
			});
			return VideoPick::Skip;
		}
		band->playback().videoPickRetryPending = false;
		band->playback().videoSinkRetries = 0;
		if (!this->warnedVideoSinkGiveUp) {
			this->warnedVideoSinkGiveUp = true;
			this->logVideo(QStringLiteral("no video sink for the panel after %1 retries; showing images instead")
				.arg(this->retryAttempts));
		}
		return VideoPick::Fallback;
	}
	band->playback().videoSinkRetries = 0;
	band->playback().videoPickRetryPending = false;
	// The clip may already be parked: consuming it makes the swap an opacity change. Drawing again would skip the
	// parked file and shift the seeded sequence by one draw per swap.
	const QString parkedPath = slot->preloadedPath();
	if (!parkedPath.isEmpty()) {
		const qint64 parkedOffsetMs = slot->preloadedOffsetMs();
		if (!band->playback().candidate.path.isEmpty()) {
			const QString dropped = band->playback().candidate.path;
			band->playback().candidate = MascotMedia();
			this->syncPin(dropped);
		}
		band->playback().prefetchPath.clear();
		this->preparePick(state, bandIndex, updateHighlight);
		this->startVideoLoad(state, panel, bandIndex, parkedPath, parkedOffsetMs);
		return VideoPick::Started;
	}
	this->applySeedSource();
	this->source.setWeightedChoice(this->weightedRandom);
	const QString path = this->source.nextVideoPath();
	if (path.isEmpty())
		return VideoPick::Fallback;
	if (!band->playback().candidate.path.isEmpty()) {
		const QString dropped = band->playback().candidate.path;
		band->playback().candidate = MascotMedia();
		this->syncPin(dropped);
	}
	band->playback().prefetchPath.clear();
	this->preparePick(state, bandIndex, updateHighlight);
	this->startVideoLoad(state, panel, bandIndex, path, -1);
	return VideoPick::Started;
}

void MascotController::startVideoLoad(PanelState& state, MascotPanel panel, int bandIndex,
	const QString& path, qint64 offsetMs)
{
	MascotBand* band = state.band(bandIndex);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!state.surface || !band || !slot || path.isEmpty())
		return;
	// The sink may be gone even though pickVideo() saw it: a mirror copy replays the left's file and its own delegate
	// may be a turn behind. The load is retried with the same file and offset, because that offset is the pair's invariant.
	if (!band->ensureVideo(0)) {
		if (band->playback().videoLoadRetries++ < this->retryAttempts) {
			this->logVideo(QStringLiteral("%1 has no video sink yet; retrying the same load")
				.arg(bandLabel(panel, bandIndex)));
			band->playback().videoPickRetryPending = true;
			QTimer::singleShot(this->retryIntervalMs, this, [this, panel, bandIndex, path, offsetMs]() {
				if (!this->running)
					return;
				PanelState& panelState = this->stateFor(panel);
				MascotBand* panelBand = panelState.band(bandIndex);
				if (panelBand && panelBand->video())
					this->startVideoLoad(panelState, panel, bandIndex, path, offsetMs);
			});
			return;
		}
		band->playback().videoLoadRetries = 0;
		band->playback().videoPickRetryPending = false;
		slot->discardPreload();
		if (!this->warnedVideoSinkGiveUp) {
			this->warnedVideoSinkGiveUp = true;
			this->logVideo(QStringLiteral("%1 could not start its load (%2 retries, no sink); the next swap re-pairs the panels")
				.arg(bandLabel(panel, bandIndex))
				.arg(this->retryAttempts));
		}
		return;
	}
	band->playback().videoLoadRetries = 0;
	band->playback().videoPickRetryPending = false;
	if (panel == MascotPanel::Background)
		this->backgroundEmptyPoolLogged = false;
	// The output is made visible before the clip is asked for, so the sink receives frames while the first-frame gate waits.
	if (!band->videoOutputVisible())
		band->setVideoOutputVisible(true);
	// The parked clip is a moment: a preload for another file *or another offset* is a miss, or the pair ends on two frames of one file.
	const bool fromPreload = slot->preloadedPath() == path && slot->hasPreload()
		&& (offsetMs < 0 || slot->preloadedOffsetMs() == offsetMs);
	const qint64 effectiveOffsetMs = fromPreload ? slot->preloadedOffsetMs() : offsetMs;
	band->setRequestPath(path);
	band->setRequestIsVideo(true);
	band->setWantedOffsetMs(effectiveOffsetMs);
	band->playback().requestedAtMs = QDateTime::currentMSecsSinceEpoch();
	++band->playback().videoSeq;
	// A left video load is what the pair follows: arm the right band's copy here, because a swap served from a preload completes before nextImages() could.
	if (panel == MascotPanel::Left && this->mirrorEnabled()) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = true;
	}
	this->logVideo(QStringLiteral("%1 swap #%2 pool=%3 offset=%4 path=\"%5\" preloaded=%6")
		.arg(bandLabel(panel, bandIndex))
		.arg(band->playback().videoSeq)
		.arg(this->source.videoPoolSize())
		.arg(effectiveOffsetMs < 0 ? QStringLiteral("random") : QString::number(effectiveOffsetMs))
		.arg(path)
		.arg(fromPreload ? QStringLiteral("yes") : QStringLiteral("no")));
	if (fromPreload && slot->swapToPreloaded())
		return;
	slot->swapTo(path, offsetMs);
}

void MascotController::onVideoFirstFrame(MascotPanel panel, int bandIndex, const QString& path,
	int ab, const QSize& frameSize, qint64 offsetMs)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || !band->video())
		return;
	if (!band->requestIsVideo() || band->requestPath() != path)
		return;

	const qint64 delta = QDateTime::currentMSecsSinceEpoch() - band->playback().requestedAtMs;
	MascotMedia media;
	media.type = MascotMedia::Video;
	media.path = path;
	media.nativeSize = frameSize;
	media.startOffsetMs = offsetMs;

	this->showMedia(state, panel, media, false, false, bandIndex);
	this->fadeInVideo(state, panel, bandIndex, ab);
	this->restartClipTimer(panel, bandIndex);
	this->logVideo(QStringLiteral("%1 first frame #%2 ab=%3 %4x%5 offset=%6ms delta=%7ms path=\"%8\"")
		.arg(bandLabel(panel, bandIndex))
		.arg(band->playback().videoSeq)
		.arg(ab)
		.arg(frameSize.width())
		.arg(frameSize.height())
		.arg(offsetMs)
		.arg(delta)
		.arg(path));

	// The left frame is where the shared offset finally exists, so this is the moment the right band can start on it.
	if (panel == MascotPanel::Left) {
		MascotBand* rightBand = this->rightState.band(bandIndex);
		if (rightBand && rightBand->playback().mirrorCopyPending) {
			rightBand->playback().mirrorCopyPending = false;
			// The copy is the mirror key's work, so it asks the key: a copy armed while the pair was mirrored must not survive Mirror off.
			if (this->mirrorEnabled() && this->rightState.surface && rightBand->video())
				this->startMirrorVideo(bandIndex, path, offsetMs);
		}
	}
}

void MascotController::fadeInVideo(PanelState& state, MascotPanel panel, int bandIndex, int ab)
{
	MascotBand* band = state.band(bandIndex);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!state.surface || !band || !slot || ab < 0 || ab > 1)
		return;
	const int fade = this->videoCrossfades(panel) ? this->crossfadeMs : 0;
	band->setVideoOpacity(ab, 1.0, fade);
	band->setVideoOpacity(1 - ab, 0.0, fade);
	if (fade <= 0) {
		slot->retireInactive();
		// No fade to wait for, so the outgoing player retires now; only then is the idle output the one the next swap uses.
		if (bandIndex == 0)
			this->preloadNext(panel);
		return;
	}
	QTimer::singleShot(fade, this, [this, panel, bandIndex]() {
		PanelState& panelState = this->stateFor(panel);
		MascotBand* panelBand = panelState.band(bandIndex);
		if (MascotVideoSlot* panelSlot = panelBand ? panelBand->video() : nullptr)
			panelSlot->retireInactive();
		if (bandIndex == 0)
			this->preloadNext(panel);
	});
}

void MascotController::preloadNext(MascotPanel panel)
{
	QMetaObject::invokeMethod(this, [this, panel] { this->startPreload(panel); }, Qt::QueuedConnection);
}

void MascotController::startPreload(MascotPanel panel)
{
	if (panel == MascotPanel::Background)
		return;
	if (!this->running || !this->preloadEnabled || !this->mediaMayBeVideo(panel))
		return;
	// A preload needs a second player to park on, and only `Static`'s crossfade has one; asking in a panning mode would draw a file nothing could use.
	if (this->layoutFor(panel).mode != MascotMode::Static)
		return;
	PanelState& state = this->stateFor(panel);
	MascotSurface* surface = state.surface.data();
	MascotBand* band = state.band(0);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!surface || !band || !slot)
		return;
	if (this->videoPaused || !surface->isVisible())
		return;
	if (band->media().type != MascotMedia::Video || band->media().path.isEmpty())
		return;
	if (slot->isBusy() || slot->hasPreload())
		return;
	if (panel == MascotPanel::Right && this->mirrorEnabled())
		return;
	if (!band->ensureVideo(0))
		return;
	if (this->pickType(state, panel) != MascotMedia::Video)
		return;
	this->applySeedSource();
	this->source.setWeightedChoice(this->weightedRandom);
	const QString path = this->source.nextVideoPath();
	if (path.isEmpty())
		return;
	slot->preload(path, -1);
}

void MascotController::onVideoPreloaded(MascotPanel panel, int bandIndex, const QString& path,
	qint64 offsetMs, const QSize& frameSize)
{
	if (!this->running || path.isEmpty())
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || !band->video())
		return;
	this->logVideo(QStringLiteral("%1 preloaded path=\"%2\" offset=%3ms %4x%5")
		.arg(bandLabel(panel, bandIndex))
		.arg(path)
		.arg(offsetMs)
		.arg(frameSize.width())
		.arg(frameSize.height()));
	if (panel != MascotPanel::Left || !this->mirrorEnabled())
		return;
	PanelState& right = this->rightState;
	MascotBand* rightBand = right.band(bandIndex);
	MascotVideoSlot* rightVideo = rightBand ? rightBand->video() : nullptr;
	if (!right.surface || !rightBand || !rightVideo)
		return;
	if (rightVideo->isBusy() || rightVideo->hasPreload())
		return;
	if (!rightBand->ensureVideo(0))
		return;
	rightVideo->preload(path, offsetMs);
}

void MascotController::startMirrorVideo(int bandIndex, const QString& path, qint64 offsetMs)
{
	PanelState& right = this->rightState;
	MascotBand* rightBand = right.band(bandIndex);
	if (!right.surface || !rightBand || !rightBand->video() || path.isEmpty())
		return;
	this->preparePick(right, bandIndex, false);
	this->startVideoLoad(right, MascotPanel::Right, bandIndex, path, offsetMs);
}

void MascotController::onVideoFailed(MascotPanel panel, int bandIndex, const QString& path,
	MascotVideoSlot::FailKind kind, bool timedOut)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || !band->video())
		return;
	// A failed file is dropped for the session, and only a *decode* failure may make that statement: a timeout is
	// the user's judgement of their machine, and a wiring report means nothing was attempted. Dropping either would
	// turn one slow open or one missing delegate into a file the user never sees again.
	if (!timedOut && kind == MascotVideoSlot::FailKind::Decode)
		this->source.noteVideoFailure(path);
	const bool wasLoading = band->requestIsVideo() && band->requestPath() == path;
	if (wasLoading) {
		band->setRequestIsVideo(false);
		band->setRequestPath(QString());
		band->setWantedOffsetMs(-1);
		++band->playback().videoFailures;
	}
	if (panel == MascotPanel::Left) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = false;
		if (wasLoading)
			this->logVideo(QStringLiteral("the mirrored pair is unpaired for now; the next swap re-pairs it"));
	}
	const QString kindLabel = timedOut ? QStringLiteral("timeout")
		: (kind == MascotVideoSlot::FailKind::Decode ? QStringLiteral("decode") : QStringLiteral("wiring"));
	const QString key = path + QStringLiteral("|") + kindLabel;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool repeat = band->playback().lastFailedVideoPath == key
		&& (now - band->playback().lastFailedVideoLogMs) < VIDEO_LOG_INTERVAL_MS;
	if (!repeat) {
		band->playback().lastFailedVideoPath = key;
		band->playback().lastFailedVideoLogMs = now;
		this->logVideo(QStringLiteral("%1 %2 #%3 onScreen=%4 path=\"%5\" kind=%6")
			.arg(bandLabel(panel, bandIndex))
			.arg(timedOut ? QStringLiteral("timed out") : QStringLiteral("failed"))
			.arg(band->playback().videoSeq)
			.arg(band->media().path.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
			.arg(path)
			.arg(kindLabel));
	}
	if (!band->media().path.isEmpty()
		&& (bandIndex == 0 || band->media().type == MascotMedia::Video)) {
		return;
	}
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
		return;
	if (band->playback().videoFailures > VIDEO_FAILURE_LIMIT) {
		if (band->playback().videoFailures == VIDEO_FAILURE_LIMIT + 1) {
			this->logVideo(QStringLiteral("%1 gave up after %2 failures in a row; the next beat or clip timer tries again")
				.arg(bandLabel(panel, bandIndex))
				.arg(VIDEO_FAILURE_LIMIT));
		}
		return;
	}
	this->queueBandRefill(panel, bandIndex, false);
}

bool MascotController::ensureVideoSlots()
{
	bool downgraded = false;
	bool refillExtraBands = false;
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		if (!state.surface)
			continue;
		const bool pairPanel = panel != MascotPanel::Background;
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			const bool mayOwnPlayer = this->bandMayOwnPlayer(panel, i);
			if (mayOwnPlayer && !band->video()) {
				this->createVideoSlot(state, panel, i);
				// A band just given a player is still showing what it showed without one, and nothing else would ask again.
				if (pairPanel && band->media().type != MascotMedia::Video)
					this->queueBandRefill(panel, i, false);
			}
			else if (!mayOwnPlayer && band->video()) {
				if (QTimer* timer = band->clipTimer())
					timer->stop();
				// A clip on screen has no player behind it any more, so the claim must go: a band still claiming video would be
				// skipped by the beat and have no clip timer -- frozen on a frame nothing owns.
				const bool hadVideo = band->media().type == MascotMedia::Video;
				if (hadVideo) {
					const QString previousShown = band->media().path;
					band->forgetMedia();
					this->syncPin(previousShown);
				}
				this->destroyVideoSlot(state, i);
				if (hadVideo || band->media().path.isEmpty()) {
					if (pairPanel) {
						if (i == 0)
							downgraded = true;
						else
							refillExtraBands = true;
					}
				}
			}
			if (MascotVideoSlot* slot = band->video())
				slot->setSinglePlayer(!this->videoCrossfades(panel));
		}
	}
	if (refillExtraBands) {
		QMetaObject::invokeMethod(this, [this]() { this->fillEmptyBands(); }, Qt::QueuedConnection);
	}
	return downgraded;
}

void MascotController::createVideoSlot(PanelState& state, MascotPanel panel, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || band->video())
		return;
	band->ensureVideo(0);
	MascotVideoSlot* slot = band->video();
	if (!slot)
		return;
	slot->setSinglePlayer(!this->videoCrossfades(panel));
	slot->setRandomStart(this->randomStart, this->clipSecondsFor(panel));
	slot->setLowLatency(this->lowLatency);
	slot->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
	connect(slot, &MascotVideoSlot::firstFrame, this,
		[this, panel, bandIndex](const QString& path, int ab, const QSize& frameSize, qint64 offsetMs) {
			this->onVideoFirstFrame(panel, bandIndex, path, ab, frameSize, offsetMs);
		});
	connect(slot, &MascotVideoSlot::failed, this,
		[this, panel, bandIndex](const QString& path, MascotVideoSlot::FailKind kind) {
			this->onVideoFailed(panel, bandIndex, path, kind, false);
		});
	connect(slot, &MascotVideoSlot::timedOut, this,
		[this, panel, bandIndex](const QString& path) {
			this->onVideoFailed(panel, bandIndex, path, MascotVideoSlot::FailKind::Decode, true);
		});
	connect(slot, &MascotVideoSlot::preloaded, this,
		[this, panel, bandIndex](const QString& path, qint64 offsetMs, const QSize& frameSize) {
			this->onVideoPreloaded(panel, bandIndex, path, offsetMs, frameSize);
		});
	// A panel built while the window cannot be seen starts paused; the pause path early-outs when nothing changed, so this cannot be inherited.
	slot->setPaused(this->videoPaused);
}

void MascotController::destroyVideoSlot(PanelState& state, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!band || !band->video())
		return;
	if (QTimer* timer = band->clipTimer())
		timer->stop();
	band->destroyVideo();
	band->setRequestIsVideo(false);
	band->setWantedOffsetMs(-1);
	band->playback().mirrorCopyPending = false;
	band->playback().videoPickRetryPending = false;
	band->playback().videoLoadRetries = 0;
}

void MascotController::destroyVideoSlot(PanelState& state)
{
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i)
		this->destroyVideoSlot(state, i);
}

void MascotController::applyDecoderBudget()
{
	// The ceiling is spent in pairs: `Static` keeps two players on each panel, and a panning mode keeps two on each
	// *band*, so the same crossfade reaches every mode and only the count of pairs differs. Crossfading every band is
	// the mode only when the ceiling covers a pair for each of them; below that every band the budget reaches takes
	// the hard cut, which is still better than showing images.
	const int crossfadeWant = PLAYERS_PER_PANEL * VIDEO_PANEL_COUNT;
	const int wantedBands = this->bandCount(MascotPanel::Left) + this->bandCount(MascotPanel::Right);
	VideoMode mode = VideoMode::Off;
	if (this->columnsLayout.mode == MascotMode::Static) {
		if (this->maxDecoders >= crossfadeWant)
			mode = VideoMode::Crossfade;
		else if (this->maxDecoders >= VIDEO_PANEL_COUNT)
			mode = VideoMode::SinglePlayer;
	}
	else if (wantedBands > 0) {
		if (this->maxDecoders >= wantedBands * PLAYERS_PER_PANEL)
			mode = VideoMode::Crossfade;
		else if (this->maxDecoders > 0)
			mode = VideoMode::SinglePlayer;
	}
	this->videoMode = mode;
	if (this->loggedDecoderBudget == this->maxDecoders || !this->mediaSettingWantsVideo())
		return;
	this->loggedDecoderBudget = this->maxDecoders;
	if (this->columnsLayout.mode == MascotMode::Static) {
		switch (mode) {
		case VideoMode::Crossfade:
			this->logVideo(QStringLiteral("mascots_max_decoders=%1 covers the %2 players two panels crossfade with; crossfade on")
				.arg(this->maxDecoders)
				.arg(crossfadeWant));
			break;
		case VideoMode::SinglePlayer:
			this->logVideo(QStringLiteral("mascots_max_decoders=%1 is below the %2 two panels crossfading want; one player per panel, hard cut")
				.arg(this->maxDecoders)
				.arg(crossfadeWant));
			break;
		case VideoMode::Off:
			this->logVideo(QStringLiteral("mascots_max_decoders=%1 is below the %2 players two panels need; no video")
				.arg(this->maxDecoders)
				.arg(VIDEO_PANEL_COUNT));
			break;
		}
		return;
	}
	switch (mode) {
	case VideoMode::Crossfade:
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 covers %2 %3 bands with two players each; crossfade on")
			.arg(this->maxDecoders)
			.arg(wantedBands)
			.arg(modeLabel(this->columnsLayout.mode)));
		break;
	case VideoMode::SinglePlayer:
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 is below the %2 players %3 bands want for a crossfade; one player each, hard cut")
			.arg(this->maxDecoders)
			.arg(wantedBands * PLAYERS_PER_PANEL)
			.arg(modeLabel(this->columnsLayout.mode)));
		break;
	case VideoMode::Off:
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 leaves no players; no video").arg(this->maxDecoders));
		break;
	}
}

void MascotController::applyBandBudget()
{
	if (this->columnsLayout.mode == MascotMode::Static) {
		this->leftBandPlayers = 0;
		this->rightBandPlayers = 0;
		this->loggedBandBudgetDecoders = -1;
		this->loggedBandBudgetWanted = -1;
		this->loggedBandBudgetAllowed = -1;
		this->loggedBandBudgetLeft = -1;
		this->loggedBandBudgetRight = -1;
		return;
	}
	const int wantedLeft = this->bandCount(MascotPanel::Left);
	const int wantedRight = this->bandCount(MascotPanel::Right);
	const int wanted = wantedLeft + wantedRight;
	// What one band costs the ceiling: a crossfading band holds two players, a hard-cutting one holds a single player.
	const int perBand = this->videoCrossfades(MascotPanel::Left) ? PLAYERS_PER_PANEL : 1;
	int allowedLeft = wantedLeft;
	int allowedRight = wantedRight;
	if (wanted * perBand > this->maxDecoders) {
		// Spend a short budget one band's worth of players at a time, alternating panels and taking each panel's bands
		// from index 0, so both keep video for as long as the budget lasts and the spare goes to the left -- the band
		// the mirror pair follows.
		allowedLeft = 0;
		allowedRight = 0;
		int remaining = std::max(0, this->maxDecoders);
		while (remaining >= perBand) {
			bool took = false;
			for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
				if (remaining < perBand)
					break;
				int& allowed = side == MascotSide::Left ? allowedLeft : allowedRight;
				const int want = side == MascotSide::Left ? wantedLeft : wantedRight;
				if (allowed < want) {
					++allowed;
					remaining -= perBand;
					took = true;
				}
			}
			if (!took)
				break;
		}
	}
	// The pair shares one video budget: a panel whose first band has no player can be neither copied into nor advanced.
	// If the clamp leaves either first band without one, neither panel keeps a player and both show images on the beat.
	bool pairDegraded = false;
	if (allowedLeft == 0 || allowedRight == 0) {
		pairDegraded = allowedLeft > 0 || allowedRight > 0;
		allowedLeft = 0;
		allowedRight = 0;
	}
	this->leftBandPlayers = allowedLeft;
	this->rightBandPlayers = allowedRight;
	if (!this->mediaSettingWantsVideo())
		return;
	const int allowed = allowedLeft + allowedRight;
	if (this->loggedBandBudgetDecoders == this->maxDecoders
		&& this->loggedBandBudgetWanted == wanted
		&& this->loggedBandBudgetAllowed == allowed
		&& this->loggedBandBudgetLeft == allowedLeft
		&& this->loggedBandBudgetRight == allowedRight
		&& this->loggedBandBudgetMode == this->columnsLayout.mode)
		return;
	this->loggedBandBudgetDecoders = this->maxDecoders;
	this->loggedBandBudgetWanted = wanted;
	this->loggedBandBudgetAllowed = allowed;
	this->loggedBandBudgetLeft = allowedLeft;
	this->loggedBandBudgetRight = allowedRight;
	this->loggedBandBudgetMode = this->columnsLayout.mode;
	if (pairDegraded) {
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 cannot give both panels a player set (%2 %3 bands ask for %4 each); neither panel keeps one, so both show images on the beat")
			.arg(this->maxDecoders)
			.arg(wanted)
			.arg(modeLabel(this->columnsLayout.mode))
			.arg(perBand));
		return;
	}
	if (allowed < wanted) {
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 is below what %2 %3 bands ask for (one player each); %4 bands keep a player (left %5, right %6) and the rest show images")
			.arg(this->maxDecoders)
			.arg(wanted)
			.arg(modeLabel(this->columnsLayout.mode))
			.arg(allowed)
			.arg(allowedLeft)
			.arg(allowedRight));
		return;
	}
	this->logVideo(QStringLiteral("mascots_max_decoders=%1 covers %2 %3 bands with %4 players each (left %5, right %6); %7")
		.arg(this->maxDecoders)
		.arg(wanted)
		.arg(modeLabel(this->columnsLayout.mode))
		.arg(perBand)
		.arg(allowedLeft)
		.arg(allowedRight)
		.arg(perBand == PLAYERS_PER_PANEL ? QStringLiteral("crossfade on") : QStringLiteral("hard cut")));
}

void MascotController::applyBackgroundBudget()
{
	// Columns first, background last: the pair keeps exactly its entitlement and the layer is spent what is left.
	// A pair that builds no players is not charged: with `mascots` off the layer gets all of the budget.
	int pairPlayers = this->leftBandPlayers + this->rightBandPlayers;
	if (this->columnsLayout.mode == MascotMode::Static) {
		const int perPanel = this->videoMode == VideoMode::Crossfade ? PLAYERS_PER_PANEL
			: (this->videoMode == VideoMode::SinglePlayer ? 1 : 0);
		pairPlayers = perPanel * VIDEO_PANEL_COUNT;
	}
	else if (this->videoMode == VideoMode::Crossfade) {
		// Each of those bands is holding a pair, so the layer may only spend what the pairs leave.
		pairPlayers *= PLAYERS_PER_PANEL;
	}
	if (!this->mediaMayBeVideo(MascotPanel::Left) && !this->mediaMayBeVideo(MascotPanel::Right))
		pairPlayers = 0;
	if (!this->backgroundEnabled) {
		this->backgroundPlayers = 0;
		this->backgroundCrossfades = false;
		this->loggedBackgroundDecoders = -1;
		this->loggedBackgroundPlayers = -1;
		this->loggedBackgroundBands = -1;
		this->loggedBackgroundMode = MascotMode::Static;
		return;
	}
	const int remaining = std::max(0, this->maxDecoders - pairPlayers);
	const MascotMode mode = this->backgroundLayout.mode;
	int players = 0;
	if (mode == MascotMode::Static) {
		players = std::min(remaining, PLAYERS_PER_PANEL);
	}
	else {
		const int wantedBands = this->backgroundStripes;
		players = std::min(remaining, wantedBands);
	}
	this->backgroundPlayers = players;
	this->backgroundCrossfades = mode == MascotMode::Static && players == PLAYERS_PER_PANEL;
	const int bands = this->backgroundBandCount();
	if (this->loggedBackgroundDecoders == this->maxDecoders
		&& this->loggedBackgroundPlayers == players
		&& this->loggedBackgroundBands == bands
		&& this->loggedBackgroundMode == mode)
		return;
	this->loggedBackgroundDecoders = this->maxDecoders;
	this->loggedBackgroundPlayers = players;
	this->loggedBackgroundBands = bands;
	this->loggedBackgroundMode = mode;
	if (players <= 0) {
		this->logBackground(QStringLiteral("mascots_max_decoders=%1: no player left for the background after the columns' %2; the layer stays blank")
			.arg(this->maxDecoders)
			.arg(pairPlayers));
		return;
	}
	this->logBackground(QStringLiteral("mascots_max_decoders=%1: the background keeps %2 player(s) across %3 band(s) in %4, %5")
		.arg(this->maxDecoders)
		.arg(players)
		.arg(bands)
		.arg(modeLabel(mode))
		.arg(this->backgroundCrossfades ? QStringLiteral("crossfade") : QStringLiteral("hard cut")));
}

void MascotController::restartClipTimer(MascotPanel panel, int bandIndex)
{
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	if (!band->clipTimer()) {
		QTimer* timer = band->ensureClipTimer();
		connect(timer, &QTimer::timeout, this, [this, panel, bandIndex]() {
			this->onClipTimer(panel, bandIndex);
		});
	}
	QTimer* timer = band->clipTimer();
	const int clip = this->clipSecondsFor(panel);
	if (clip <= 0 || this->videoPaused) {
		timer->stop();
		return;
	}
	// Under the mirror key one timer advances the pair; two armed a few milliseconds apart would start two loads per clip.
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex)) {
		timer->stop();
		return;
	}
	timer->start(std::min(clip * 1000, MAX_CLIP_TIMER_MS));
}

void MascotController::onClipTimer(MascotPanel panel, int bandIndex)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!band || !band->video() || band->media().type != MascotMedia::Video)
		return;
	this->logVideo(QStringLiteral("MASCOT-ADVANCE %1 band=%2 source=clip").arg(panelLabel(panel)).arg(bandIndex));
	if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
		this->nextBandImages(bandIndex, false);
		return;
	}
	this->pickFor(state, panel, bandIndex, bandIndex == 0);
}

void MascotController::stopClipTimers()
{
	for (PanelState* state : { &this->leftState, &this->rightState, &this->backgroundState }) {
		for (auto& owned : state->bands) {
			if (QTimer* timer = owned->clipTimer())
				timer->stop();
		}
	}
}

bool MascotController::videoShouldPause() const
{
	if (!this->pauseUnfocused)
		return false;
	QWidget* window = this->watchedWindow.data();
	if (!window)
		return false;
	if (!window->isVisible() || window->isMinimized())
		return true;
	// The rule is "the window can be seen", not focus: visible, not minimized, not covered.
	return !this->windowSeen;
}

void MascotController::refreshWindowSeen()
{
	QWidget* window = this->watchedWindow.data();
	const bool seen = window && utils::IsMyWindowVisible(window);
	if (this->windowSeenKnown && seen == this->windowSeen)
		return;
	this->windowSeen = seen;
	this->windowSeenKnown = true;
	this->logVideo(QStringLiteral("MASCOT-VISIBLE seen %1").arg(seen));
	this->applyVideoPaused();
	this->updateScrollDriver();
}

bool MascotController::previewTooltipBusy() const
{
	// The popup's visibility is not the answer -- it is shown before its first frame and destroyed after its last.
	MainWindow* window = this->App ? this->App->mainWindow : nullptr;
	if (!window)
		return false;
	PreviewVideoWindow* tooltip = window->previewTooltip.data();
	return tooltip && tooltip->isPlaying();
}

void MascotController::applyVideoPaused()
{
	if (!this->running)
		return;
	const bool pause = this->videoShouldPause();
	if (pause == this->videoPaused)
		return;
	this->videoPaused = pause;
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			MascotVideoSlot* slot = band ? band->video() : nullptr;
			if (!slot)
				continue;
			slot->setPaused(pause);
			if (pause) {
				if (QTimer* timer = band->clipTimer())
					timer->stop();
				// A parked clip goes with the cadence, and nothing may advance while nothing can be seen: a timer that fired
				// here would start a load the gate reads as a swap.
				slot->discardPreload();
			}
			else if (band->media().type == MascotMedia::Video && !band->media().path.isEmpty()) {
				this->restartClipTimer(panel, i);
				if (i == 0 && panel != MascotPanel::Background)
					this->preloadNext(panel);
			}
		}
	}
	if (this->mediaSettingWantsVideo() || this->backgroundBandCount() > 0) {
		this->logVideo(pause ? QStringLiteral("paused (the window cannot be seen)")
			: QStringLiteral("resumed (the window is visible)"));
	}
}
void MascotController::watchWindow(QWidget* window)
{
	if (this->watchedWindow == window)
		return;
	if (this->watchedWindow)
		this->watchedWindow->removeEventFilter(this);
	this->watchedWindow = window;
	if (!window)
		return;
	window->installEventFilter(this);
	this->windowSeenKnown = false;
	this->refreshWindowSeen();
}

void MascotController::logVideo(const QString& message) const
{
	// The tag is in the message as well as in the Logger's type: the gate reads the message only.
	if (!this->App || !this->App->debug_mode || !this->App->logger)
		return;
	this->App->logger->log(VIDEO_LOG_TAG + message, QStringLiteral("MASCOT-VIDEO"));
}

void MascotController::logScroll(const QString& message) const
{
	if (!this->App || !this->App->debug_mode || !this->App->logger)
		return;
	this->App->logger->log(SCROLL_LOG_TAG + message, QStringLiteral("MASCOT-SCROLL"));
}

void MascotController::logBackground(const QString& message) const
{
	if (!this->App || !this->App->debug_mode || !this->App->logger)
		return;
	this->App->logger->log(BACKGROUND_LOG_TAG + message, QStringLiteral("MASCOT-BG"));
}

void MascotController::connectBeats()
{
	if (this->beatConnection)
		return;
	if (!this->App || !this->App->MascotsAnimation)
		return;
	// One connection to the pulse: connecting the legacy signals as well would handle every beat twice.
	this->beatConnection = connect(this->App->MascotsAnimation, &mascotsAnimationsThread::beatPulse,
		this, &MascotController::onBeat);
}

void MascotController::disconnectBeats()
{
	if (this->beatConnection)
		disconnect(this->beatConnection);
	this->beatConnection = QMetaObject::Connection();
}

void MascotController::onBeat(bool swapWanted)
{
	// A video band's cadence is its clip timer, not the beat: a beat arrives far more often than any clip length.
	if (!this->running || !this->beatsAllowed())
		return;
	// The layer's cadence rides the swap beat only; it has no flip state and no pair. The block sits immediately after
	// the visibility guard, before the pair's flip path. The layer rolls its own dice.
	if (this->backgroundSwapOnBeat && this->backgroundState.surface
		&& this->backgroundRandomChange && utils::randfloat(0, 1) < this->backgroundRandomChance) {
		for (int i = 0; i < static_cast<int>(this->backgroundState.bands.size()); ++i) {
			MascotBand* band = this->backgroundState.band(i);
			if (!band)
				continue;
			this->scheduleBeatAdvance(MascotPanel::Background, i);
		}
	}
	if (!swapWanted || !this->swapOnBeat) {
		if (this->columnsEnabled)
			this->flipBoth();
		return;
	}
	if (!this->columnsEnabled)
		return;
	if (this->columnsLayout.mode == MascotMode::Static) {
		// `Static` is M1's cadence, untouched: the pair advances together.
		// Under the mirror key the pair advances only when the *oldest* of its two commits meets the floor, or the two columns show different files.
		MascotBand* leftBand = this->leftState.band(0);
		MascotBand* rightBand = this->rightState.band(0);
		const bool leftMay = leftBand != nullptr;
		const bool rightMay = rightBand != nullptr;
		const bool mirrors = this->bandMirrorsLeft(0);
		if (mirrors) {
			if (this->leftState.surface && this->rightState.surface && leftMay && rightMay) {
				this->nextBandImages(0, false);
				return;
			}
			this->flipBoth();
			return;
		}
		if (leftMay && rightMay) {
			this->nextBandImages(0, false);
			return;
		}
		if (leftMay) {
			this->pickFor(this->leftState, MascotPanel::Left, 0, true);
			return;
		}
		if (rightMay) {
			this->pickFor(this->rightState, MascotPanel::Right, 0, false);
			return;
		}
		this->flipBoth();
		return;
	}
	bool advanced = false;
	const int bandTotal = std::max(
		static_cast<int>(this->leftState.bands.size()),
		static_cast<int>(this->rightState.bands.size()));
	for (int i = 0; i < bandTotal; ++i) {
		if (this->bandMirrorsLeft(i)) {
			MascotBand* leftBand = this->leftState.band(i);
			MascotBand* rightBand = this->rightState.band(i);
			if (!this->leftState.surface || !this->rightState.surface || !leftBand || !rightBand
				)
				continue;
			advanced = true;
			this->scheduleBeatAdvance(MascotPanel::Left, i);
			continue;
		}
		for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
			PanelState& state = this->stateFor(panelOf(side));
			if (!state.surface)
				continue;
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			advanced = true;
			this->scheduleBeatAdvance(panelOf(side), i);
		}
	}
	if (!advanced)
		this->flipBoth();
}

int MascotController::clipSecondsFor(MascotPanel panel) const
{
	return panel == MascotPanel::Background ? this->backgroundClipSeconds : this->clipSeconds;
}

bool MascotController::bandReachesSeam(MascotPanel panel, const MascotBand& band) const
{
	const PanelLayout& layout = this->layoutFor(panel);
	if (!this->running || layout.mode == MascotMode::Static)
		return false;
	if (!this->scrollTimer || !this->scrollTimer->isActive())
		return false;
	if (!this->scrollCanTick())
		return false;
	if (layout.scrollSpeed <= 0)
		return false;
	if (panel == MascotPanel::Background && layout.mode == MascotMode::Scroll) {
		if (!this->backgroundMarqueeActive(panel))
			return false;
		const PanelState& marqueeOwner = this->stateFor(panel);
		if (!marqueeOwner.surface)
			return false;
		const QRectF frame(this->panelFrame(panel));
		const double axisLen = this->scrollDirectionIsVertical(panel) ? frame.height() : frame.width();
		return axisLen > 0.0;
	}
	const double travel = this->scrollTravel(panel, band);
	if (travel <= 0.0)
		return false;
	const PanelState& owner = this->stateFor(panel);
	const int localIndex = band.slotIndex() - owner.slotBase;
	if (localIndex < 0 || localIndex >= static_cast<int>(owner.bands.size())
		|| owner.band(localIndex) != &band)
		return false;
	double referenceTravel = 0.0;
	for (const auto& owned : owner.bands)
		referenceTravel = std::max(referenceTravel, this->scrollTravel(panel, *owned));
	if (referenceTravel <= 0.0)
		return false;
	const double tickDelta = static_cast<double>(layout.scrollSpeed)
		* (static_cast<double>(SCROLL_TICK_MS) / 1000.0) / referenceTravel;
	return tickDelta < 1.0;
}

void MascotController::scheduleBeatAdvance(MascotPanel panel, int bandIndex)
{
	this->logVideo(QStringLiteral("MASCOT-ADVANCE %1 band=%2 source=beat").arg(panelLabel(panel)).arg(bandIndex));
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	if (this->bandReachesSeam(panel, *band)) {
		band->queueWrapCommit([this, panel, bandIndex]() {
			this->commitBeatAdvance(panel, bandIndex);
		});
		return;
	}
	this->commitBeatAdvance(panel, bandIndex);
}

void MascotController::commitBeatAdvance(MascotPanel panel, int bandIndex)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	if (!state.surface)
		return;
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
		return;
	// The commit may run at a seam a whole clip after the beat, so the conditions are asked again -- a swap the current
	// configuration does not permit must not run. The per-panel key, not the columns' one: the layer's `_swap_on_beat` is independent.
	const bool swapOnBeat = panel == MascotPanel::Background ? this->backgroundSwapOnBeat : this->swapOnBeat;
	if (!swapOnBeat)
		return;
	if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
		MascotBand* rightBand = this->rightState.band(bandIndex);
		if (!rightBand)
			return;
		this->nextBandImages(bandIndex, false);
		return;
	}
	this->pickFor(state, panel, bandIndex, bandIndex == 0);
}

bool MascotController::beatsAllowed() const
{
	return this->panelsVisible();
}

void MascotController::onMediaReady(const MascotMedia& media)
{
	if (!this->running || media.path.isEmpty())
		return;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		if (!state.surface)
			continue;
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.bands[static_cast<std::size_t>(i)].get();
			if (!band)
				continue;
			if (band->requestIsVideo())
				continue;
			if (band->requestPath() != media.path)
				continue;
			this->showMedia(state, panelOf(side), media, false, true, i);
		}
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band || band->requestIsVideo())
				continue;
			if (band->playback().prefetchPath == media.path) {
				band->playback().prefetchPath.clear();
				this->holdCandidate(*band, media);
			}
		}
	}
}

void MascotController::onMediaFailed(const QString& path)
{
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		// A failed fill on a band past the first is retried here -- it has no beat above it -- queued and bounded by its own failure budget.
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			if (band->requestIsVideo())
				continue;
			if (band->requestPath() != path)
				continue;
			band->setRequestPath(QString());
			if (i == 0 || !band->media().path.isEmpty())
				continue;
			const int failures = ++band->playback().imageFailures;
			if (failures > VIDEO_FAILURE_LIMIT) {
				if (failures == VIDEO_FAILURE_LIMIT + 1) {
					this->logVideo(QStringLiteral("%1 gave up after %2 failed fills; the next layout or media change tries again")
						.arg(bandLabel(panelOf(side), i))
						.arg(VIDEO_FAILURE_LIMIT));
				}
				continue;
			}
			this->queueBandRefill(panelOf(side), i, true);
		}
		for (auto& owned : state.bands) {
			if (owned->playback().prefetchPath == path)
				owned->playback().prefetchPath.clear();
		}
	}
}

void MascotController::queueBandRefill(MascotPanel panel, int bandIndex, bool onlyIfEmpty)
{
	if (!this->running)
		return;
	if (panel == MascotPanel::Background)
		return;
	QMetaObject::invokeMethod(this, [this, panel, bandIndex, onlyIfEmpty]() {
		if (!this->running)
			return;
		PanelState& panelState = this->stateFor(panel);
		MascotBand* band = panelState.band(bandIndex);
		if (!panelState.surface || !band)
			return;
		if (onlyIfEmpty) {
			if (!band->media().path.isEmpty())
				return;
		}
		else if (band->media().type == MascotMedia::Video && !band->media().path.isEmpty()) {
			return;
		}
		if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
			return;
		if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
			this->nextBandImages(bandIndex, false);
			return;
		}
		this->pickFor(panelState, panel, bandIndex, bandIndex == 0);
	}, Qt::QueuedConnection);
}
