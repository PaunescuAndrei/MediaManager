#include "stdafx.h"
#include "MascotController.h"
#include "MainApp.h"
#include "MainWindow.h"
// §7.13's preview rule asks the tooltip's own player whether it is playing. MainWindow holds it by
// QPointer and its isPlaying() is public, so nothing about the preview's ownership changes: this
// include is the whole cost of the question.
#include "PreviewVideoWindow.h"
#include "VideosModel.h"
#include "utils.h"
#include <QDateTime>
#include <QEvent>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
	// The tag every video line carries, in the message as well as in the Logger's type: the line
	// is read from OutputDebugString by the gate's harness, which only sees the message.
	const QString VIDEO_LOG_TAG = QStringLiteral("MASCOT-VIDEO: ");

	// Two panels, two players each for the crossfade: the budget `mascots_max_decoders` has to
	// fit before the full design is possible. The second tier -- one player per panel -- is what
	// a budget between the two numbers degrades to.
	constexpr int VIDEO_PANEL_COUNT = 2;
	constexpr int PLAYERS_PER_PANEL = 2;

	// A crossfade longer than this is not a crossfade any more, and a clip length of weeks is a
	// timer nobody can observe. Both are clamped where they are read, so a bad config value can
	// never produce an arithmetic surprise further down.
	constexpr int MAX_CROSSFADE_MS = 10000;
	constexpr int MAX_CLIP_SECONDS = 86400;
	constexpr int MAX_CLIP_TIMER_MS = MAX_CLIP_SECONDS * 1000;

	// A panel that has failed this many times in a row stops re-arming itself: a library of files
	// the backend cannot decode would otherwise be walked through at load speed, one line each,
	// for as long as the panel is on screen. The next beat or clip timer starts it again.
	constexpr int VIDEO_FAILURE_LIMIT = 3;

	// A panel whose QML delegate does not exist yet has no sink, and a pick that started a load
	// into nothing would be reported as a failure of the file. The pick is retried this many
	// times, this far apart, before the panel falls back to images: a delegate is normally there
	// by the time the surface has applied its slot count, so this is the belt to that braces.
	// (Task 6: the two numbers this comment introduced are `mascots_retry_attempts` and
	// `mascots_retry_interval_ms` now, read and clamped in applyConfig() and pushed with
	// MascotVideoSlot::setTimings(). The prose above is why those are the defaults -- the constants
	// it described are gone because the values are the user's to set (§7.11).)

	// §7.13's preview rule is a deferral, not a skip: a tooltip plays for as long as the user keeps
	// hovering, so the pick is re-asked at this interval until it stops. One `isPlaying()` on a
	// QPointer is the whole cost of a tick, and the panel keeps the clip it is already showing --
	// the rule gates a *start*, never playback.
	// (Task 6: "this interval" is `mascots_busy_defer_ms` now, and the constant this comment used to
	// name is gone with it. The rate limit below is unchanged and still exists for the reason it
	// gives -- a per-deferral log line would fight the swap lines the gate reads.)

	// The rate limit on the two lines that can be produced by every swap (which branch picked and
	// "the pool is empty"): the statement is about the configuration, and repeating it at swap
	// rate would bury the swap and first-frame lines the gate reads.
	constexpr qint64 VIDEO_LOG_INTERVAL_MS = 5000;

	// `left` / `right` as the log spells them, so a line can be read without knowing the enum.
	QString sideLabel(MascotSide side)
	{
		return side == MascotSide::Left ? QStringLiteral("left") : QStringLiteral("right");
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

	// One configured number with a documented default, for the waits that gate a user-visible wait
	// (Task 6). A value that does not parse is *not* clamped: toInt() answers 0 for "abc", and
	// clamped to the floor that is the most aggressive setting there is -- a typo in config.ini
	// would silently shorten the swap deadline to 500 ms and start abandoning clips that are merely
	// opening. So a malformed value falls back to the default the spec documents, and the clamp only
	// ever applies to a number the user actually wrote. (`fallback` is inside [low, high] by
	// construction, so the clamp is a no-op for it.)
	int clampedConfigInt(Config* config, const QString& key, int fallback, int low, int high)
	{
		if (!config)
			return fallback;
		bool ok = false;
		const int value = config->get(key).toInt(&ok);
		return std::clamp(ok ? value : fallback, low, high);
	}

	// M3 Task 2: `count` equal slices of one panel along its stacking axis, which is what a
	// panel's bands are. `stackVertically` is the horizontal-stripe axis: `Static`, `Scroll` and
	// `Stripes` all stack top to bottom for now, and Task 5's `mascots_stripes_orientation` is
	// what adds the column variant -- it changes this one argument, not the slicing below.
	//
	// A slice's end is the *next* slice's start, derived from the panel's own edges and rounded
	// to whole pixels, so the slices always sum to the panel exactly. Computing a height by
	// division instead leaves the last band a pixel short (or over) whenever the panel does not
	// divide evenly by the band count, which is a visible seam or a band overflowing the panel.
	QVector<QRectF> slicedBandRects(const QRectF& panel, int count, bool stackVertically)
	{
		QVector<QRectF> rects;
		if (count <= 0 || panel.width() <= 0.0 || panel.height() <= 0.0)
			return rects;
		// One band is the panel itself, not a rounded copy of it: `Static` and `Scroll` must write
		// the exact rectangle they wrote before a mode existed, and this branch is that guarantee.
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
}

MascotController::MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
	QWidget* leftHost, QWidget* rightHost, QObject* parent)
	: QObject(parent), source(App)
{
	this->App = App;
	this->cache = cache;
	this->prefetcher = prefetcher;
	this->leftHost = leftHost;
	this->rightHost = rightHost;

	// mediaReady and mediaFailed are emitted on the GUI thread (the prefetcher queues its
	// decode results back to it), so these connections are direct and a media is applied in
	// the same turn it was published. A request that produces nothing simply produces no
	// signal: no slot waits for it (amendment H1).
	MascotPrefetcher* source = this->prefetcher.data();
	if (source) {
		connect(source, &MascotPrefetcher::mediaReady, this, &MascotController::onMediaReady);
		connect(source, &MascotPrefetcher::mediaFailed, this, &MascotController::onMediaFailed);
	}
}

MascotController::~MascotController()
{
	// Releases the pins and the surfaces even on a path that never called stop().
	this->stop();
}

void MascotController::setSurfaces(MascotSurface* left, MascotSurface* right)
{
	if (this->leftState.surface == left && this->rightState.surface == right) {
		this->updateLayout();
		return;
	}
	// Whatever the old surfaces were showing is released first: their slots, their pins and
	// their pending requests must not survive into the new ones.
	const MascotSurface* previousLeft = this->leftState.surface.data();
	const MascotSurface* previousRight = this->rightState.surface.data();
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
	// A surface this call replaces is released exactly the way stop()/disable releases it: it is
	// deleted, so a controller handed a fresh pair does not leave the previous panels alive --
	// and still drawing -- inside their hosts. A surface that is passed again is kept.
	if (previousLeft && previousLeft != left)
		this->releaseSurface(this->leftState);
	if (previousRight && previousRight != right)
		this->releaseSurface(this->rightState);
	this->leftState.surface = left;
	this->rightState.surface = right;
	this->configureSurface(this->leftState, MascotSide::Left);
	this->configureSurface(this->rightState, MascotSide::Right);
	this->updateLayout();
}

void MascotController::start()
{
	if (this->running)
		return;
	// Config first: createSurfaces() applies it to the slots it builds.
	this->applyConfig();
	this->createSurfaces();
	this->running = true;
	this->connectBeats();
	// M2: the pause state is derived from the window the surfaces have just landed in, so a panel
	// enabled while the window is minimized or in the background starts paused instead of loading
	// a clip nobody can see.
	this->applyVideoPaused();
	// Both panels were empty when the old code ran updateMascots() right before
	// showMascots(); one seeding swap does both jobs now.
	this->nextImages(false);
	// M3 Task 2: `nextImages()` seeds the band the panel's policy lives on; a `Stripes` panel has
	// more than one band, and the rest are filled here so an enabled Stripes panel is never half
	// blank. A one-band panel is asked for nothing at all -- it has already been seeded, and this
	// is the only place `Static` and `Scroll` could have paid an extra pick they never paid
	// before -- while in Stripes the loop's per-band guards skip the band that is already loading.
	if (this->bandCount(MascotSide::Left) > 1 || this->bandCount(MascotSide::Right) > 1)
		this->fillEmptyBands();
}

void MascotController::stop()
{
	this->running = false;
	this->disconnectBeats();
	// The cadence, the pause state and the window watch belong to a running controller: a clip
	// timer that survived a disable would advance a panel that is no longer there, and the next
	// start() re-derives the pause from the window it finds.
	this->stopClipTimers();
	this->watchWindow(nullptr);
	this->videoPaused = false;
	this->sawWindowActivation = false;
	// M2: the players come down before the surfaces are released, because the sinks they draw
	// into belong to the surfaces' QML delegates (Task 2's interface fact). shutdown() detaches
	// them synchronously, so a delegate destroyed next cannot leave a player pointing at it.
	// (M3 Task 2: every band of both panels, not only band 0. A `Stripes` panel can own players on
	// any band once Task 5 gives it a budget; leaving bands 1..n out of this loop would let them
	// keep drawing into delegates the release below destroys -- the carried-forward Task 1 finding
	// this loop exists to close for good. In `Static` the loop runs once per panel.)
	for (SideState* state : { &this->leftState, &this->rightState }) {
		for (auto& owned : state->bands) {
			if (owned->video())
				owned->video()->shutdown();
		}
	}
	// Nothing may be applied after this point: clear() drops the queued notifications and
	// makes every in-flight decode and every already-posted republication unpublishable, so a
	// decode that finishes later cannot resurrect a slot that is being cleared.
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->clear();
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
	this->releaseSurface(this->leftState);
	this->releaseSurface(this->rightState);
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

	// Amendment D's `mascots_media` is read here so it is not a write-only key. M1 renders
	// images only; a value this milestone cannot honour is reported rather than ignored. Said
	// once per process: it is a statement about the configuration, not an event, so repeating it
	// on every Apply would only bury the rest of the log.
	// M2 honours all three values, so the warning below now covers only a value that is none of
	// them; the two sentences above stand as the record of what this line asserted in M1. The six
	// video keys are read in the same place and for the same reason -- this method is this
	// class's only config reader -- and pickType() decides from the cached values, so a settings
	// change cannot land one pick late and no swap path reads the config at all.
	this->mediaSetting = config->get(QStringLiteral("mascots_media"));
	this->clipSeconds = clampedClipSeconds(config);
	this->randomStart = config->get_bool(QStringLiteral("mascots_random_start"));
	this->crossfadeMs = clampedCrossfadeMs(config);
	this->maxDecoders = std::max(0, config->get(QStringLiteral("mascots_max_decoders")).toInt());
	this->pauseUnfocused = config->get_bool(QStringLiteral("mascots_pause_unfocused"));
	this->weightedRandom = config->get_bool(QStringLiteral("mascots_weighted_random"));
	// Task 6: the preload is a setting, not a decision made for the user. On a machine where a
	// parked extra frame costs more than it saves, the pre-Task-6 behaviour (choose and load at the
	// swap) is the better trade, and it is the same path a panel falls back to whenever a preload
	// cannot be made -- so turning the key off cannot be a different code path, only a different
	// starting point.
	this->preloadEnabled = config->get_bool(QStringLiteral("mascots_preload"));
	// The waits that gate a user-visible wait (§7.11), clamped here so a config file cannot produce
	// a deadline that abandons clips that are merely opening. Each floor and ceiling is the range
	// within which the value still means what the key says:
	//  - the swap deadline [500, 60000]: 500 ms is already outside every measured warm load (49-300
	//    ms in the M2 gate), so a shorter one would drop good clips, and above a minute it is not a
	//    deadline any more -- `isBusy()` would hold a dead file's panel for that long. It has no
	//    "off" value on purpose: this timer is the only thing that releases a load that neither
	//    paints nor errors.
	//  - the retry interval [20, 2000]: under a frame at any refresh rate there is nothing new to
	//    find, and over two seconds the retry is slower than the delegate it waits for.
	//  - the retry budget [0, 50]: 0 is a supported "do not retry, give up at once"; 50 is above any
	//    count a human would configure and bounds the churn a missing delegate can cause.
	//  - the preview deferral [50, 5000]: a shorter interval re-asks a player that cannot have
	//    started, a longer one leaves the panel waiting on a tooltip that may already be over.
	// A value that does not parse falls back to the documented default rather than to the floor --
	// clampedConfigInt() explains why that matters for a deadline.
	this->swapTimeoutMs = clampedConfigInt(config, QStringLiteral("mascots_swap_timeout_ms"), 5000, 500, 60000);
	this->retryIntervalMs = clampedConfigInt(config, QStringLiteral("mascots_retry_interval_ms"), 150, 20, 2000);
	this->retryAttempts = clampedConfigInt(config, QStringLiteral("mascots_retry_attempts"), 5, 0, 50);
	this->busyDeferMs = clampedConfigInt(config, QStringLiteral("mascots_busy_defer_ms"), 250, 50, 5000);

	// M3 Task 2: the layout mode and the two band counts `Stripes` stacks, read here under the
	// same "one reader" rule as every key above: applyMode() and bandCount() answer from these
	// cached values, so a settings change cannot land one layout late and no layout path re-reads
	// the config. A value that does not parse is not clamped into range -- clampedConfigInt()
	// explains why that matters for a count a typo would otherwise turn into the floor.
	// `mascots_stripes_orientation` and `mascots_stripes_alternate` are deliberately *not* read
	// yet: Task 5 owns both, and until then a Stripes panel stacks horizontally (the documented
	// default) with no phase stagger, which is the shape the two counts alone describe.
	bool modeOk = false;
	const QString modeValue = config->get(QStringLiteral("mascots_mode"));
	const MascotMode parsedMode = MascotController::modeFromString(modeValue, &modeOk);
	this->mascotMode = modeOk ? parsedMode : MascotMode::Static;
	this->stripesLeftBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_left"), 2, 1, 8);
	this->stripesRightBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_right"), 2, 1, 8);
	// M3 Task 3: the fill mode. Read here under the same "one reader" rule as every key above:
	// effectiveFillMode() answers from this cached value, so a settings change cannot land one
	// repaint late and no repaint path re-reads the config. The documented default is
	// `HeightFit` -- what M1 shipped (the retired renderer's scaled(w, height, KeepAspectRatio))
	// and what every existing panel already draws -- so a value this class cannot honour must not
	// reshape a picture just because the key is absent or mistyped (Ruling M3-2).
	bool fillOk = false;
	const QString fillValue = config->get(QStringLiteral("mascots_fill"));
	const MascotSurface::FillMode parsedFill = MascotController::fillModeFromString(fillValue, &fillOk);
	this->fillModeSetting = fillOk ? parsedFill : MascotSurface::FillMode::HeightFit;

	// A value that is none of the three is reported once per process rather than ignored: it is a
	// statement about the configuration, not an event, so repeating it on every Apply would only
	// bury the rest of the log.
	if (!this->warnedUnsupportedMedia && !this->mediaSetting.isEmpty()
		&& this->mediaSetting.compare(QStringLiteral("Images"), Qt::CaseInsensitive) != 0
		&& this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) != 0
		&& this->mediaSetting.compare(QStringLiteral("Mixed"), Qt::CaseInsensitive) != 0) {
		this->warnedUnsupportedMedia = true;
		this->logVideo(QStringLiteral("mascots_media=\"%1\" is not one of Images, Videos or Mixed; showing images.")
			.arg(this->mediaSetting));
		// The debug line above is not enough on its own: a typo here ("video", "Video ", "Mixed ")
		// leaves both panels on images in every run, and a debug-gated line is invisible in exactly
		// the run where the user needs it. So M1's ungated warning is restored beside the debug
		// line rather than replaced by it, once per process like it was, with its wording moved to
		// the values this milestone accepts. It is not routed through the Logger for the same
		// reason: the Logger's OutputDebugString copy is the debug-only half.
		qWarning() << "mascots_media:" << this->mediaSetting
			<< "is not one of Images, Videos or Mixed; showing images.";
	}

	// M3 Task 2: the same one-warning latch as `mascots_media`, for the same reason -- an
	// unsupported value is a statement about the configuration, not an event, so repeating it on
	// every Apply would only bury the rest of the log. `Static` is the documented default and the
	// shape every panel had before a mode existed, so it is the honest fallback. An empty value is
	// not reported: that is the key simply not being written, which `Config` answers with the
	// default, and it is the same rule the media warning above applies.
	if (!this->warnedUnsupportedMode && !modeValue.isEmpty() && !modeOk) {
		this->warnedUnsupportedMode = true;
		this->logVideo(QStringLiteral("mascots_mode=\"%1\" is not one of Static, Scroll or Stripes; using Static")
			.arg(modeValue));
		// The ungated warning beside the debug line, exactly as the media warning has it: a typo
		// here leaves every panel in the layout the user did not ask for, and a debug-gated line
		// is invisible in the run where they need to see it.
		qWarning() << "mascots_mode:" << modeValue
			<< "is not one of Static, Scroll or Stripes; using Static.";
	}

	// M3 Task 3: the same one-warning latch as the two blocks above, for the same reason -- an
	// unsupported value is a statement about the configuration, not an event, so repeating it on
	// every Apply would only bury the rest of the log. The fallback is `HeightFit` rather than the
	// spec table's original `Crop`, so a typo leaves every panel exactly as it was instead of
	// reshaping all of them. An empty value is not reported: that is the key simply not being
	// written, which `Config` answers with the default.
	if (!this->warnedUnsupportedFill && !fillValue.isEmpty() && !fillOk) {
		this->warnedUnsupportedFill = true;
		this->logVideo(QStringLiteral("mascots_fill=\"%1\" is not one of Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit")
			.arg(fillValue));
		// The ungated warning beside the debug line, exactly as the two blocks above have it: a
		// typo here reshapes every panel, and a debug-gated line is invisible in the run where the
		// user needs to see why their picture changed.
		qWarning() << "mascots_fill:" << fillValue
			<< "is not one of Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit.";
	}

	this->source.setAllFilesRandom(config->get_bool("mascots_allfiles_random"));
	// The weighted/uniform switch is one of the source's two config-derived inputs, and the
	// source reads no config: it is pushed here and again before every video pick, so a change
	// made while the panels are running applies from the next pick rather than from the next
	// Apply.
	this->source.setWeightedChoice(this->weightedRandom);
	this->applySeedSource();
	// The decoder budget is applied (and its clamp line written) before the panels are built, so
	// the mode the log names is the mode the players are created with.
	this->applyDecoderBudget();
	// M3 Task 2: the layout comes before anything that writes into a band, because the mode
	// decides how many bands a panel has and where they are. A mode change resizes the surface
	// the panel already has -- the surface is the thing drawing, so it is never replaced here --
	// and a panel whose mode and band count did not move is left completely alone.
	this->applyMode();

	const bool mirror = this->mirrorEnabled();
	// M3 Task 2: the player half below stays on `band(0)` for now. The extra bands a `Stripes`
	// panel has are image-only until Task 5 gives each one a player inside the decoder budget, so
	// there is no second band whose players these live settings could have to reach -- which is
	// why the loop reads the panel's policy band and not every band.
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		MascotBand* band = state->band(0);
		// The surface-wide default. The per-slot value written by applyDisplay() below is
		// what is drawn, so the default can never disagree with the picture.
		state->surface->setMirror(mirror);
		// Applying config drops a user flip, which is what the retired renderer did when the
		// settings were applied: the side rule becomes authoritative and the repaint below draws
		// what the settings say, never a leftover toggle.
		state->flipToggled = false;
		// The clip rules are live settings as much as the mirror is, and createVideoSlot() must not
		// be their only reader: `mascots_clip_seconds` also re-arms the clip timer below, so a slot
		// still holding the previous length picks its start offset from a range that length no
		// longer means -- 30 s down to 5 s leaves a 30 s range, and a clip can then open 5 s before
		// the end and visibly restart mid-clip. `mascots_random_start` off fails the other way: a
		// slot created under `True` keeps randomising until it is rebuilt. Pushed here, so one
		// Apply moves the timer and the offset range together instead of letting two consumers of
		// one key disagree.
		if (band && band->video())
			band->video()->setRandomStart(this->randomStart, this->clipSeconds);
		// The same "one key, one reader" rule for the waits: a slot created earlier has to hear the
		// new deadline and the new retry budget, or the two consumers of `mascots_swap_timeout_ms`
		// (this class's retry timers and the slot's own deadline) would disagree until the panel was
		// rebuilt. Pushed here rather than only in createVideoSlot() for exactly the reason
		// setRandomStart() is.
		if (band && band->video())
			band->video()->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
		// The preload switch is a live setting too, and turning it *off* has to be more than a guard
		// on the next preload: a clip parked before the Apply would still be there, hold its paused
		// decoder, and be served by the next swap as `preloaded=yes` -- i.e. the switch would only
		// take effect one clip later, and the panel would show behaviour the user had just turned
		// off. Discarded here so "off" means off from the next swap, exactly as it does from
		// startup. An armed preload goes with it: discardPreload() drops both states.
		if (band && band->video() && !this->preloadEnabled)
			band->video()->discardPreload();
	}
	// A mode change repaints what is already on screen; the surface default alone would only
	// affect a slot created later.
	this->applyDisplay(this->leftState, MascotSide::Left);
	this->applyDisplay(this->rightState, MascotSide::Right);
	this->updateLayout();
	// The player half follows the new media setting and the new budget: an `Images` setting, or a
	// budget below what a panel needs, gives the panel back its memory instead of leaving idle
	// players behind, and a setting that becomes video-capable builds them.
	const bool downgraded = this->ensureVideoSlots();
	// A config apply can change `mascots_pause_unfocused` or the clip length under a running
	// panel, so the new rules are applied to what is playing now.
	this->applyVideoPaused();
	// The clip timer is re-derived as well, because the mirror key and the clip length are both
	// inputs to it: the right panel's timer is stopped while the pair is mirrored, so turning the
	// key off mid-run would otherwise leave that panel on its clip until its next commit. The cost
	// is that an Apply pushes the next swap out by a whole clip, which is what the new length asks
	// for anyway.
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		MascotBand* band = state.band(0);
		if (band && band->media().type == MascotMedia::Video && !band->media().path.isEmpty())
			this->restartClipTimer(state, side);
	}
	// A panel that lost its players is drawing nothing, and nothing else will fill it: the beat
	// skips a panel by what it shows (which is now nothing) and the clip timer belonged to players
	// that no longer exist. So the apply owes the pair one pick -- queued, because this runs inside
	// a settings handler and a pick walks the mascot directory and posts a decode. One pick for the
	// pair rather than one per panel, so the mirror key keeps deciding whether the two panels show
	// the same file.
	if (downgraded && this->running) {
		this->logVideo(QStringLiteral("panels downgraded from video to images; repainting them"));
		QMetaObject::invokeMethod(this, [this]() { this->nextImages(false); }, Qt::QueuedConnection);
	}
}

void MascotController::applySeedSource()
{
	if (!this->App || !this->App->config)
		return;
	Config* config = this->App->config;
	// The seed string and the two switches the retired generator read, with the same precedence:
	// seeding applies only while both `random_use_seed` and `mascots_use_seed` are on.
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
	// The panel's historical shape, and what applyConfig() falls back to: a caller that ignores
	// `ok` still gets a mode that draws something rather than an uninitialised one.
	return MascotMode::Static;
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
	if (ok)
		*ok = false;
	// The mode M1 shipped, and what applyConfig() falls back to: a caller that ignores `ok` still
	// gets the height-normalised rendering every existing panel has, which is also the only
	// fallback that cannot change a picture just because a key was misspelled (Ruling M3-2).
	return MascotSurface::FillMode::HeightFit;
}

MascotSurface::FillMode MascotController::effectiveFillMode() const
{
	// `Static` is the one mode whose band is the whole panel and therefore has no panning axis to
	// normalise, so it honours the configured value exactly -- which is what makes `Crop`, `Fit`,
	// `Stretch` and `WidthFit` selectable at all.
	//
	// `Scroll` and `Stripes` pan along the axis their bands are stacked along, and a pan needs
	// that axis normalised: the band's content must be exactly as tall (or wide) as the band in
	// the other axis, or the pan would also have to solve a second overflow. This task's stacking
	// axis is horizontal (`slicedBandRects(..., true)`), so the normalised mode is `HeightFit`,
	// whose width is the only axis that overflows. M3 Task 5 makes the `Stripes` answer follow
	// `mascots_stripes_orientation`, where a vertical stripe stacks along the other axis and
	// therefore needs `WidthFit`; no `Stripes` panel this task can produce does.
	if (this->mascotMode == MascotMode::Static)
		return this->fillModeSetting;
	return MascotSurface::FillMode::HeightFit;
}

int MascotController::bandCount(MascotSide side) const
{
	// `Static` and `Scroll` are one band by definition -- Scroll's band *is* the panel, and only
	// the content inside it moves (Task 4) -- so this task's whole layout asks the same question
	// of both, and a one-band panel is exactly the panel that existed before a mode did.
	if (this->mascotMode != MascotMode::Stripes)
		return 1;
	return side == MascotSide::Left ? this->stripesLeftBands : this->stripesRightBands;
}

QVector<QRectF> MascotController::bandRects(MascotSide side) const
{
	const SideState& state = this->stateFor(side);
	if (!state.surface)
		return QVector<QRectF>();
	// The mode's stacking axis. Every mode this task reads stacks its bands top to bottom (the
	// `Stripes` default orientation, because Task 5 owns the key that would choose otherwise), so
	// a band's rectangle is a horizontal slice of the panel. Slicing the surface's own rectangle
	// is what keeps every band inside the panel and their sum equal to the panel itself.
	return slicedBandRects(QRectF(state.surface->rect()), this->bandCount(side), true);
}

void MascotController::applyMode()
{
	// Nothing to lay out yet: applyConfig() runs before createSurfaces() on the enable path, so
	// the first Apply has no panel to act on. configureSurface() applies the same layout when the
	// surface does appear and start() fills the bands it has; nothing is recorded here, which is
	// what makes the next Apply with surfaces run this again.
	if (!this->leftState.surface && !this->rightState.surface)
		return;
	// Idempotent by construction: an Apply that changed neither the mode nor either panel's live
	// band count has nothing to re-derive, and re-running the fill would walk the mascot directory
	// for bands that already have content.
	const bool modeChanged = this->mascotMode != this->appliedMode;
	bool countChanged = false;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		const SideState& state = this->stateFor(side);
		if (state.surface && state.configuredBandCount != this->bandCount(side))
			countChanged = true;
	}
	if (this->modeApplied && !modeChanged && !countChanged)
		return;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (state.surface)
			this->syncBands(state, side);
	}
	this->appliedMode = this->mascotMode;
	this->modeApplied = true;
	// The rectangles follow a count or a mode change, and the surface is resized in place rather
	// than replaced: a band that survives the change keeps its media and its players (Review
	// Focus 1), and the thing drawing the panel never goes away underneath it. A first run after
	// the surfaces were built has nothing to re-write here -- configureSurface() already gave
	// them this layout and its caller wrote the rectangles -- so `Static` stays on exactly the
	// surface writes it made before modes existed.
	// (M3 fix: gated on the count, not on the mode. The rectangles are derived from the band count
	// and the stacking axis, never from the mode itself, so a mode change that leaves both alone
	// -- `Static` to `Scroll`, which are both one full-panel band -- would only re-write the rects
	// applyConfig() has already written, and a settings Apply would then reach each band's
	// setSlotRect twice. M3 Task 5 adds `mascots_stripes_orientation`: that key changes the axis
	// without changing the count, so its own relayout has to be added to this condition.)
	if (countChanged)
		this->updateLayout();
	// Bands the change added have no content. Queued, because this runs inside a settings handler
	// and a pick walks the mascot directory and posts a decode; a controller that was stopped in
	// between simply never runs it, and fillEmptyBands() asks the same guards again.
	QMetaObject::invokeMethod(this, [this]() { this->fillEmptyBands(); }, Qt::QueuedConnection);
}

void MascotController::syncBands(SideState& state, MascotSide side)
{
	MascotSurface* surface = state.surface.data();
	if (!surface)
		return;
	const int wanted = this->bandCount(side);
	// Shrink from the tail, before the slot count shrinks. A band's players draw into QVideoSinks
	// that belong to its slot's QML delegate, and MascotVideoSlot's contract is that those sinks
	// outlive its shutdown() -- so the bands that are going must be destroyed while their delegate
	// still exists. This is the same order releaseSurface() keeps for a whole surface, applied to
	// the bands a count change removes.
	QStringList removedShown;
	while (static_cast<int>(state.bands.size()) > wanted) {
		removedShown.push_back(state.bands.back()->media().path);
		state.bands.pop_back();
	}
	// The count is what makes QML create the delegates the rectangles below are written into, so
	// it goes before the geometry. Shrinking here is safe: the bands past the new count are
	// already gone, and setSlotCount() preserves the slot state of the indices that survive.
	surface->setSlotCount(wanted);
	// A surviving band keeps its media, its request and its players (Review Focus 1: a mode change
	// must not re-decode what stays on screen). setSurface() is a no-op for the surface a band
	// already draws into, and the one thing a *rebuilt* surface needs -- it retires the slot state
	// that described the panel that has just gone.
	for (auto& owned : state.bands)
		owned->setSurface(surface);
	// New indices last: a new band is created for the slot the count above has just made, and the
	// first thing it is told is which surface hosts it.
	while (static_cast<int>(state.bands.size()) < wanted) {
		const int index = static_cast<int>(state.bands.size());
		auto created = std::make_unique<MascotBand>(index);
		created->setSurface(surface);
		state.bands.push_back(std::move(created));
	}
	state.configuredBandCount = wanted;
	// The destroyed bands are no longer owner fields, so the pin invariant is re-established for
	// each path they held: the fields are gone first and every path whose ownership that changed
	// is re-evaluated afterwards, exactly as every other path in this class does it.
	for (const QString& path : removedShown)
		this->syncPin(path);
}

void MascotController::fillEmptyBands()
{
	if (!this->running)
		return;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface)
			continue;
		const int count = static_cast<int>(state.bands.size());
		for (int i = 0; i < count; ++i) {
			MascotBand* band = state.band(i);
			if (!band || !band->media().path.isEmpty())
				continue;
			// A request in flight *is* this band's content: asking again would post a second
			// decode for one band, and for band 0 would walk the mascot directory twice for one
			// fill. `isPending` is asked rather than the path alone for the reason pickFor() asks
			// it -- a request that produced nothing at all must not wedge the band.
			if (!band->requestPath().isEmpty()) {
				MascotPrefetcher* source = this->prefetcher.data();
				if (!source || source->isPending(band->requestPath()))
					continue;
			}
			if (i == 0) {
				// The panel's policy band takes the panel's own pick -- the call the seeding swap,
				// the beat and the clip timer all make -- so a single-band panel (`Static`,
				// `Scroll`) fills exactly as it did before bands were counted, candidate path and
				// all. That is the loop's one iteration in `Static`.
				this->pickFor(state, side, true);
				continue;
			}
			// A band past the first has no player in this task: the per-band decoder budget is
			// Task 5's, and the cadence that would advance an extra band is Task 6's. That makes
			// it deliberately an *image* band -- filled once here through the same source and the
			// same request/delivery path an image pick uses, and then left showing that picture
			// until the next layout change. The degradation has a known end, and the alternative
			// this task could have offered is a band with no media at all, which is worse than a
			// still.
			//
			// Drawing the path here rather than calling pickFor() is deliberate: pickFor() owns
			// the panel's single candidate slot, its speculation and its video sequence, all of
			// which belong to band 0 until Task 5/6 give the bands their own. A pick that consumed
			// the panel's candidate for a picture on band 1 would cost band 0 its predecoded swap,
			// and one that started a video load would start it with no player to receive it.
			const QString path = this->nextSourcePath();
			if (path.isEmpty())
				continue;
			band->setRequestIsVideo(false);
			this->requestPath(state, path, i);
		}
	}
}

void MascotController::updateLayout()
{
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface)
			continue;
		// The slot fills the surface that hosts it, and the surface derives the content shift
		// from this rectangle itself (amendment R: no centring arithmetic here).
		// M3 Task 1: the rectangle goes through the band, which is the only thing that writes
		// into a slot now.
		// M3 Task 2: one rectangle per band, from the mode's slices of this surface. A single-band
		// panel gets the panel's own rectangle back unchanged (slicedBandRects() returns the panel
		// itself for a count of one), so `Static` writes exactly the rectangle it always wrote.
		const QVector<QRectF> rects = this->bandRects(side);
		for (int i = 0; i < static_cast<int>(state.bands.size()) && i < rects.size(); ++i)
			state.bands[static_cast<std::size_t>(i)]->setRect(rects.at(i));
	}
	// M2: the panel's window is what the pause rule watches, and this is the place a surface
	// learns which window it ended up in (it is parented to a host inside the main window). Only
	// the first call for a given window does anything: watchWindow() returns early otherwise.
	QWidget* window = nullptr;
	if (this->leftState.surface)
		window = this->leftState.surface->window();
	if (!window && this->rightState.surface)
		window = this->rightState.surface->window();
	this->watchWindow(window);
}

void MascotController::nextImage(MascotSide side, bool updateHighlight)
{
	this->pickFor(this->stateFor(side), side, updateHighlight);
}

void MascotController::nextImages(bool forcedMirror)
{
	if (!this->running)
		return;
	const MascotMedia left = this->pickFor(this->leftState, MascotSide::Left, true);
	if (this->mirrorEnabled() || forcedMirror) {
		if (!this->rightState.surface)
			return;
		// M2: a video pick cannot be copied the way an image can. An image's media is the pick
		// (the decode happens later but the media already names the file and its geometry); a
		// video's media does not exist until a frame of it reaches the sink, so what the pair has
		// to share is the file *and the start offset*, and the offset is only known once the left
		// panel's load reports it. This branch therefore either waits for that report or starts
		// the right panel from the media the left panel is already showing.
		MascotBand* leftBand = this->leftState.band(0);
		if (!leftBand)
			return;
		if (leftBand->requestIsVideo() || this->leftState.videoPickRetryPending) {
			MascotVideoSlot* leftVideo = leftBand->video();
			if (leftVideo && leftVideo->isBusy()) {
				// Still in flight: the right panel's copy is started from the left panel's
				// firstFrame(), which carries the offset the pair has to agree on.
				this->preparePick(this->rightState, false);
				this->rightState.mirrorCopyPending = true;
				return;
			}
			if (this->leftState.videoPickRetryPending) {
				// The left panel's pick is waiting for its QML delegate. That retry will start a
				// load, and that load's first frame is where the pair's offset exists -- so the copy
				// is armed now. Without this the pair would be silently unpaired for a whole clip:
				// under the mirror key nothing else ever advances the right panel.
				this->preparePick(this->rightState, false);
				this->rightState.mirrorCopyPending = true;
				return;
			}
			if (leftBand->media().type == MascotMedia::Video && !leftBand->media().path.isEmpty()) {
				// Already showing: the offset is committed, so the copy needs no report.
				this->startMirrorVideo(leftBand->media().path, leftBand->media().startOffsetMs);
				return;
			}
			// Neither loading nor showing: the left panel's pick produced nothing (its players
			// are gone, or it failed before a frame). Say nothing and let the next swap re-pair.
			return;
		}
		if (!left.path.isEmpty()) {
			// Both panels show the same media, each drawn by its own side rule, and the right
			// panel gets it from the commit that just happened: no request, no decode.
			this->preparePick(this->rightState, false);
			this->showMedia(this->rightState, MascotSide::Right, left, false, true, 0);
			return;
		}
		// The left panel's pick is still in flight (H1): copy the path it is about to show, not
		// the one it still shows, and the single delivery fills both panels.
		if (leftBand->requestPath().isEmpty())
			return;
		this->preparePick(this->rightState, false);
		this->requestPath(this->rightState, leftBand->requestPath(), 0);
	}
	else {
		this->pickFor(this->rightState, MascotSide::Right, false);
	}
}

void MascotController::flipSide(MascotSide side)
{
	SideState& state = this->stateFor(side);
	MascotBand* band = state.band(0);
	// The retired renderer's flip was a no-op on an empty pixmap, so flipping a panel that shows
	// nothing is not a state change here either.
	if (!state.surface || !band || band->media().path.isEmpty())
		return;
	state.flipToggled = !state.flipToggled;
	this->applyMirror(state, side, band->media().path, 0);
}

void MascotController::flipBoth()
{
	this->flipSide(MascotSide::Left);
	this->flipSide(MascotSide::Right);
}

bool MascotController::flipForSide(const QString& path, MascotSide side) const
{
	// MainWindow::setLeftMascot()/setRightMascot()'s rule, unchanged: a file from the `right`
	// directory is drawn mirrored on the left panel and one from `left` on the right panel.
	const QString parent = utils::getParentDirectoryName(path, 1);
	if (side == MascotSide::Left)
		return parent == QStringLiteral("right");
	return parent == QStringLiteral("left");
}

color_area MascotController::colorFor(MascotSide side, bool weighted)
{
	const SideState& state = this->stateFor(side);
	const MascotBand* band = state.band(0);
	// The retired renderer's getColor() semantics, unchanged, on the palette the decode already
	// extracted (amendment H5): weight by area when asked, otherwise take the dominant area
	// out of both lists. An invalid QColor means "no palette", which the caller ignores.
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
	// The retired MainWindow drop entry point was gated on debug mode, and a drop is the
	// same action: a user dragging a PNG onto a panel must not replace it unless the app was
	// started with -debug. Parity wins here -- replacing a panel by drag is not a feature anyone
	// asked for.
	if (!this->App || !this->App->debug_mode)
		return;
	SideState& state = this->stateFor(side);
	if (!state.surface)
		return;
	// That entry point's behaviour: the dropped file is drawn unflipped on this panel
	// (setImage(path)), and with the mirror key on it is copied onto the other panel
	// flipped (setImage(path, true)) rather than through the side-directory rule.
	state.mirrorOverrideEnabled = true;
	state.mirrorOverrideValue = false;
	state.flipToggled = false;
	state.highlightOnApply = true;
	// A drop lands on the panel, i.e. on its policy band, exactly as it did before bands existed;
	// a `Stripes` panel's extra bands are not drop targets (M3 Task 2).
	this->requestPath(state, path, 0);

	if (!this->mirrorEnabled())
		return;
	SideState& otherState = this->stateFor(other(side));
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
	if (event->type() == QEvent::Resize) {
		// The host gets its width from the layout after applyMascotWidth() returns, so the
		// surface is the only object that knows when its own rectangle is final.
		if ((this->leftState.surface && watched == this->leftState.surface.data())
			|| (this->rightState.surface && watched == this->rightState.surface.data())) {
			this->updateLayout();
		}
	}
	// M2: the pause rule (amendment §7.13). The window's own events are the only honest source
	// for "the user cannot see the panel": a minimized window is still visible in Qt's sense,
	// and a hidden one is not minimized. Each of these just re-derives the state, so a burst of
	// them costs one comparison each and cannot leave the players in the wrong state.
	if (this->watchedWindow && watched == this->watchedWindow.data()) {
		switch (event->type()) {
		case QEvent::WindowActivate:
			// From here on, "not active" means the user switched away rather than "the window
			// has not been activated yet", which is what the startup state looks like.
			this->sawWindowActivation = true;
			this->applyVideoPaused();
			break;
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::WindowDeactivate:
		case QEvent::WindowStateChange:
			this->applyVideoPaused();
			break;
		default:
			break;
		}
	}
	return QObject::eventFilter(watched, event);
}

MascotController::SideState& MascotController::stateFor(MascotSide side)
{
	return side == MascotSide::Left ? this->leftState : this->rightState;
}

const MascotController::SideState& MascotController::stateFor(MascotSide side) const
{
	return side == MascotSide::Left ? this->leftState : this->rightState;
}

MascotBand* MascotController::SideState::band(int i) const
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
	// Read at the point of use, like the old renderer did, so a settings change applies to
	// the media already on screen (applyConfig() re-writes the slot for that reason).
	if (!this->App || !this->App->config)
		return true;
	return this->App->config->get_bool("mascots_center_content");
}

void MascotController::createSurfaces()
{
	if (!this->cache)
		return;
	// One surface per host, built on demand: nothing here runs for a user who never enables
	// the renderer. The zero-margin layout is what makes the surface the size of the host, so
	// the slot rectangle the surface receives is the host's rectangle.
	auto build = [this](QWidget* host) -> MascotSurface* {
		if (!host)
			return nullptr;
		if (!host->layout()) {
			auto* layout = new QVBoxLayout(host);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);
		}
		auto* surface = new MascotSurface(this->cache, host);
		host->layout()->addWidget(surface);
		// A child created after its parent was shown is created hidden, and a hidden child is
		// skipped by the parent's own show; showing it here is what makes the panel appear on
		// the very first enable. While the host is hidden this only sets the intent, and
		// beatsAllowed() still keys off the surface actually being visible.
		surface->show();
		return surface;
	};
	MascotSurface* left = this->leftState.surface.data();
	MascotSurface* right = this->rightState.surface.data();
	if (!left)
		left = build(this->leftHost.data());
	if (!right)
		right = build(this->rightHost.data());
	this->setSurfaces(left, right);
}

void MascotController::configureSurface(SideState& state, MascotSide side)
{
	if (!state.surface)
		return;
	MascotSurface* surface = state.surface.data();
	// M3 Task 1: exactly one band for the one slot this panel has, created before the slot count
	// is written so every write below has its band. Task 2 creates one per slot instead.
	// (M3 Task 2: syncBands() is that "instead". It brings this panel's bands to the mode's count,
	// writes the slot count, and hands every band its surface -- surviving bands keep their media
	// and players, new indices get a band of their own, and a rebuilt surface retires the slot
	// state that described the panel that has just gone.)
	this->syncBands(state, side);
	// The image path's mode (M1): the media is scaled to the panel's height with its aspect
	// ratio kept, which is what the old renderer's scaled(width, height, KeepAspectRatio)
	// produced -- a tall panel keeps a narrow image's side margins and crops a wide one
	// horizontally. The surface owns that geometry (amendment R); the controller only picks the
	// mode. A later milestone that shows video picks its own mode here.
	// M2: that milestone is this one, so the write moved to applyDisplay(), which picks the mode
	// per media type -- and both types pick HeightFit (§8 M2 ruling), so what is drawn is the
	// mode this comment describes. The comment stays as the record of why.
	surface->setMirror(this->mirrorEnabled());
	// M3 Task 2: one mirror per band, from the media that band already carries. setMirror() above
	// is only the bulk default a slot created by setSlotCount() starts from; these calls are what
	// re-apply the per-band flip parity a surviving band had before the surface was rebuilt (a
	// single-band panel still makes exactly the one call it made before bands were counted).
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i)
		this->applyMirror(state, side, state.bands[static_cast<std::size_t>(i)]->media().path, i);
	surface->installEventFilter(this);
	connect(surface, &MascotSurface::clicked, this,
		[this, side](int, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
			emit this->clicked(side, button, modifiers);
		});
	connect(surface, &MascotSurface::imageDropped, this, [this, side](int, QString path) {
		this->showDroppedImage(side, path);
	});
	// M2: the player half of this panel, when the media setting and the decoder budget allow one.
	// The sinks stay unbound until the first swap (see createVideoSlot).
	// (M3 Task 1: createVideoSlot() calls the band's ensureVideo(), which binds them when the
	// delegate already exists; the sentence above is why it does not wait for them.)
	this->ensureVideoSlots();
}

void MascotController::releaseSurface(SideState& state)
{
	if (!state.surface)
		return;
	state.surface->removeEventFilter(this);
	// M2: the two caches below describe the *surface's own slot*, so they die with it. A rebuilt
	// surface starts at MascotSurface's defaults (FillMode::Fit, video outputs hidden), and a
	// surviving "the slot already carries HeightFit" would suppress the write in applyDisplay()
	// forever after -- which is exactly the M1 image-layout regression this task may not cause
	// (HeightFit is what makes an image scale to the panel's height instead of fitting inside it).
	// M3 Task 1: "the two caches below" are the band's now, and `setSurface(nullptr)` is what
	// retires them -- the band outlives the surface (a mode change in Task 2 reuses it), and a
	// band whose surface is gone must not claim its slot already carries a fill mode.
	// (M3 Task 2: every band, not just the panel's policy band. A `Stripes` panel's extra bands
	// draw into the same surface, so each of them has slot state that dies with it.)
	for (auto& owned : state.bands)
		owned->setSurface(nullptr);
	// M2: the players go before the surface. They draw into QVideoSinks that belong to the
	// surface's QML delegate, so destroying the surface first would leave them holding pointers
	// into objects that no longer exist. shutdown() is the synchronous detach Task 2 asks for.
	// (M3 Task 2: destroyVideoSlot() takes down every band's players, which is what keeps a band
	// past the first from outliving the sinks its surface owns.)
	this->destroyVideoSlot(state);
	// Synchronous, not deleteLater(): a later start() must be able to build a surface into
	// the same host, and a deferred delete would leave the old one there until the event loop
	// ran. Nothing calls stop() from a surface's own handler, so no frame is executing inside
	// the object being destroyed.
	delete state.surface.data();
	state.surface = nullptr;
}

void MascotController::clearSide(SideState& state)
{
	// M3 Task 2: every band of the panel, not only its policy band. A `Stripes` panel shows a
	// picture per band, so each band's path is an owner field the pin invariant has to see gone
	// before the pins are re-evaluated -- and each band's slot and players are part of "the panel
	// is cleared". In `Static` this is one band and the statements below are the ones this method
	// always ran.
	QStringList previousShown;
	for (auto& owned : state.bands)
		previousShown.push_back(owned->media().path);
	const QString previousCandidate = state.candidate.path;
	// M2: a cleared panel must not keep a clip playing into a slot nobody draws, and `clearSlot()`
	// deliberately leaves the video state alone (Task 1's interface fact), so both halves are done
	// here: the players are stopped and their outputs hidden.
	// (M3 Task 1: the outputs are hidden by the band's clear() below, which also empties the slot
	// and forgets the media; stopping the players is still here, because whether they live is the
	// caller's decision.)
	// (M3 Task 2: "the players" is every band's players for the same reason the paths above are.)
	if (state.clipTimer)
		state.clipTimer->stop();
	for (auto& owned : state.bands) {
		if (owned->video())
			owned->video()->stopAll();
	}
	state.mirrorCopyPending = false;
	state.videoFailures = 0;
	for (auto& owned : state.bands)
		owned->clear();
	state.prefetchPath.clear();
	state.candidate = MascotMedia();
	state.flipToggled = false;
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.highlightOnApply = false;
	// Amendment H2: the owner fields are gone before the pins are re-evaluated, so a path that
	// only this panel referenced is unpinned and one another panel still references is not.
	// A pinned entry is never evicted, which is why a missed unpin is invisible until the
	// cache has grown past every bound.
	for (const QString& path : previousShown)
		this->syncPin(path);
	this->syncPin(previousCandidate);
}

bool MascotController::pinReferenced(const QString& path) const
{
	if (path.isEmpty())
		return false;
	// Every field that keeps a media alive on screen or in reserve. A cache entry is pinned
	// exactly while one of them still names its path.
	// M3 Task 2: every band of every panel is one of those fields now. A `Stripes` picture that
	// is on screen but not named here could be evicted from the cache under the slot drawing it,
	// so the question has to be asked of all of them; a single-band panel answers exactly as it
	// did before bands were counted.
	for (const SideState* state : { &this->leftState, &this->rightState }) {
		for (const auto& owned : state->bands) {
			if (owned->media().path == path)
				return true;
		}
	}
	return this->leftState.candidate.path == path || this->rightState.candidate.path == path;
}

void MascotController::syncPin(const QString& path)
{
	if (path.isEmpty())
		return;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->pin(path, this->pinReferenced(path));
}

void MascotController::applyDisplay(SideState& state, MascotSide side)
{
	if (!state.surface)
		return;
	// M3 Task 2: every band, not only the panel's policy band -- a config change or a mode change
	// has to repaint the pictures a `Stripes` panel's extra bands are showing instead of leaving
	// the slots to whatever the surface happens to carry. In `Static` there is one band and this
	// is the single presentMedia() call it always was.
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
		MascotBand* band = state.bands[static_cast<std::size_t>(i)].get();
		if (!band)
			continue;
		// A copy, not the band's own reference: presentMedia() hands it back to setMedia(), which
		// writes the member, and a self-assignment through a reference is not something to rely on.
		const MascotMedia media = band->media();
		if (media.path.isEmpty())
			continue;
		this->presentMedia(state, side, media, i);
	}
}

void MascotController::presentMedia(SideState& state, MascotSide side, const MascotMedia& media,
	int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band)
		return;
	this->applyMirror(state, side, media.path, bandIndex);
	// M2: the fill mode is written here rather than once per surface, because it now follows the
	// media type. Both types ask for HeightFit (§8's ruling: a video resizes vertically to fit and
	// centres horizontally, exactly like an image), so in practice the value only differs from
	// M1's when a later milestone adds `mascots_fill` -- but the decision belongs next to the
	// media it describes, and the `slotFillModeKnown` test keeps the image path from writing a
	// property it already carries.
	// (M3 Task 3: "a later milestone adds `mascots_fill`" is this one. The value is no longer
	// hardcoded: effectiveFillMode() answers with the configured mode in `Static` and with the
	// mode's normalised panning axis in `Scroll`/`Stripes`, and the configured default is
	// HeightFit -- so a default config still writes exactly the value this comment describes.
	// The media type still plays no part in the choice, which is why the write stays here.)
	band->applyFill(this->effectiveFillMode());
	// M3 Task 1: the record and the write are one call on the band, and it comes after the
	// mirror and the fill mode because the slot derives its content shift from both.
	band->setMedia(media);
	if (media.type == MascotMedia::Video) {
		// A video's content shift is 0: there is no alpha silhouette to centre (the media's own
		// frame is the content), and the slot is already the frame's height-normalised rectangle.
		band->setContentOffset(QPointF(0.0, 0.0));
		// Shown, not faded in: the opacity stays where fadeInVideo() left it, which is 0 for a
		// fresh panel, so the output is receiving frames (and the gate can see the first one)
		// while nothing of it is visible yet (M2 ruling: show the output before asking for a clip).
		band->setVideoOutputVisible(true);
		return;
	}
	// The panel is back on the image branch: the video outputs go away and the players stop, so a
	// dropped image on a video panel costs a decoder for exactly as long as the drop takes. The
	// flag is what keeps an images-only configuration from writing any of this at all.
	if (band->videoOutputVisible()) {
		band->setVideoOutputVisible(false);
		if (band->video())
			band->video()->stopAll();
		// The "fresh panel -> opacity 0" premise startVideoLoad() re-shows the output on is about
		// the *slot*, and the slot outlives this branch: hiding the outputs leaves their opacity
		// where the last clip left it. After any video -> image -> video cycle on the same surface
		// (a dropped image, a `mascots_media` / `max_decoders` round trip without a recreate) the
		// next clip would then load at the previous clip's opacity -- revealing the old frozen
		// frame for the whole load on A (which single-player mode always uses) and blending that
		// dead frame into the new clip on B. Reset here, where the outputs are already hidden and
		// therefore nothing of it can be seen. Duration 0 because this is a reset, not a fade:
		// interpolating either opacity here would paint the old frame over the image this branch
		// has just shown.
		band->setVideoOpacity(0, 0.0, 0);
		band->setVideoOpacity(1, 0.0, 0);
	}
	// An image panel has no cadence of its own: M1's beat-driven behaviour is the whole of it,
	// and a clip timer left running would advance a panel the beat is already advancing.
	// (M3 Task 2: the timer belongs to the *panel*, so only the policy band's image commit may
	// stop it. A `Stripes` panel's extra band showing a still must not kill the clip timer that is
	// advancing the video on band 0 -- the per-band cadence is Task 6's, and until then the
	// panel's one timer is the only cadence there is.)
	if (bandIndex == 0 && state.clipTimer)
		state.clipTimer->stop();
	// `mascots_center_content` off means "centre the whole image, not its content", which is
	// the surface's own shift set to zero. The media write above resets any previous
	// override, so this has to come after it (amendment R: the surface owns the arithmetic,
	// this only says whether to use it).
	if (!this->centerContentEnabled())
		band->setContentOffset(QPointF(0.0, 0.0));
}

void MascotController::applyMirror(SideState& state, MascotSide side, const QString& path,
	int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band)
		return;
	bool mirrored;
	if (state.mirrorOverrideEnabled)
		mirrored = state.mirrorOverrideValue;
	else if (!path.isEmpty())
		mirrored = this->flipForSide(path, side);
	else
		mirrored = false;
	// The retired renderer toggled whatever the side rule had produced, and a new image reset that
	// toggle. One authoritative value per slot, written here and nowhere else.
	// (M3 Task 2: the toggle is per panel, so every band of a `Stripes` panel draws the same
	// parity -- one flip of the panel flips all of its bands, which is what "flip this panel"
	// means to the user.)
	if (state.flipToggled)
		mirrored = !mirrored;
	band->applyMirror(mirrored);
}

void MascotController::requestPath(SideState& state, const QString& path, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	band->setRequestPath(path);
	// M2: this is the image branch's request, so a video the panel was still loading is no longer
	// what it is waiting for -- its first frame will be ignored rather than applied over the
	// image (a dropped image on a video panel is exactly this case).
	band->setRequestIsVideo(false);
	// The pending mirror copy is the *panel pair's* state: it says the right panel owes a copy of
	// the left panel's next video commit. A `Stripes` fill of one of the extra bands is not that
	// panel's next clip, so only the policy band's request may clear it (M3 Task 2).
	if (bandIndex == 0)
		state.mirrorCopyPending = false;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

MascotMedia MascotController::pickFor(SideState& state, MascotSide side, bool updateHighlight)
{
	if (!this->running || !state.surface)
		return MascotMedia();
	MascotBand* band = state.band(0);
	if (!band)
		return MascotMedia();

	// M2: what kind of media this pick is, decided before the candidate block below because a
	// video pick has no candidate to be served from (its media is the first frame) and because an
	// image candidate left over from an image-mode pick must not be shown over a video panel.
	const MascotMedia::Type type = this->pickType(state);
	if (type == MascotMedia::Video) {
		switch (this->pickVideo(state, side, updateHighlight)) {
		case VideoPick::Started:
			return MascotMedia();
		case VideoPick::Skip:
			// A load is already in flight, or the panel is about to retry its pick. Either way
			// this swap is done: falling through would put an image over a video panel.
			return MascotMedia();
		case VideoPick::Fallback:
			// Video cannot serve this pick -- the pool emptied between the size and the draw, or
			// the player half is gone. The image path below serves it instead of leaving the panel
			// without one, and the pick line under this switch records that it did.
			break;
		}
	}
	if (this->mediaMayBeVideo() && this->App && this->App->debug_mode) {
		// One line per swap while a video configuration is in play, so the gate can see which
		// branch served each pick and how big the pool was. Never written for an Images setting,
		// which is what keeps an images run's log identical to merged master's.
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastTypeLogMs == 0 || (now - state.lastTypeLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastTypeLogMs = now;
			this->logVideo(QStringLiteral("%1 pick=image pool=%2 media=%3")
				.arg(sideLabel(side))
				.arg(this->source.videoPoolSize())
				.arg(this->mediaSetting));
		}
	}

	// A ready candidate: the media was chosen and decoded before the swap was due, so committing
	// it is a property write -- no decode, no directory walk and no request on this path. Any
	// older request this panel still had in flight is dropped by showMedia (Important #1), so a
	// late delivery for it can no longer overwrite what is shown here.
	if (!state.candidate.path.isEmpty()) {
		const MascotMedia media = state.candidate;
		this->preparePick(state, updateHighlight);
		this->showMedia(state, side, media, true, true, 0);
		return media;
	}

	// Nothing ready: the first swap after enable, or a candidate whose decode failed or was
	// dropped (amendment H1). One request in flight per panel, though: while the prefetcher is
	// still decoding the path this panel asked for, another swap must not walk the mascot
	// directory and post a second decode, because a decode slower than the beat interval would
	// then grow the pool's queue on every beat. `isPending` is asked rather than
	// `requestedPath` alone so that a request that produced nothing at all (a republish
	// dropped because its pixels had been evicted) falls through to a fresh pick instead of
	// wedging the panel.
	if (!band->requestPath().isEmpty()) {
		MascotPrefetcher* source = this->prefetcher.data();
		if (!source || source->isPending(band->requestPath()))
			return MascotMedia();
	}

	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return MascotMedia();

	this->preparePick(state, updateHighlight);
	// Band 0 is the band this pick path serves: the panel's candidate, its speculation and its
	// video sequence are all band 0's (M3 Task 2).
	this->requestPath(state, path, 0);
	return MascotMedia();
}

QString MascotController::nextSourcePath()
{
	// The retired generator's refreshRngFromConfig() re-read the seed on every pick, and a
	// settings change can land between two picks, so the source is fed at the same point rather
	// than only when applyConfig() runs.
	this->applySeedSource();
	return this->source.nextImagePath();
}

void MascotController::preparePick(SideState& state, bool updateHighlight)
{
	// A fresh pick resets what the previous media carried: the side-directory rule is
	// authoritative again, and the theme colour follows this swap only if it was asked for.
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.flipToggled = false;
	state.highlightOnApply = updateHighlight;
}

void MascotController::showMedia(SideState& state, MascotSide side, const MascotMedia& media,
	bool fromReady, bool armCandidate, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || media.path.isEmpty())
		return;
	// M3 Task 2: which of the statements below belong to the panel and which to the band. The
	// candidate, the speculation, the flip parity, the mirror pair and the theme colour are the
	// panel's policy and live on band 0; a `Stripes` panel's extra bands are filled outside all of
	// it, so their commits must not consume the panel's predecoded candidate or drop the pair's
	// pending copy. `Static` is always band 0, so this is the path it always ran.
	const bool policyBand = bandIndex == 0;

	// Amendment H2: the owner fields change first and the pins are re-evaluated afterwards, so a
	// path that stays referenced -- the candidate becoming the displayed media, or the other
	// panel showing the same file -- is never unpinned even for one statement, which would let
	// eviction take the pixels between the two.
	const QString previousShown = band->media().path;
	const QString previousCandidate = state.candidate.path;
	if (policyBand && fromReady)
		state.candidate = MascotMedia(); // consumed by this commit
	// Recorded here, before the pins are re-evaluated, and written by presentMedia() below: that
	// is the order the commit has always run in (amendment H2 needs the new path to be the shown
	// one when pinReferenced() runs, and the slot write needs the mirror settled first).
	band->recordMedia(media);
	if (policyBand)
		state.flipToggled = false;
	// A commit supersedes whatever this panel was still waiting for: a delivery for an older pick
	// must not overwrite what is on screen a beat later (Important #1). This is the one place it
	// is dropped, so every commit path -- a candidate, the mirror copy, an awaited delivery --
	// drops it, and the panel is armed for a fresh candidate below instead.
	band->setRequestPath(QString());
	// M2: the wait this commit supersedes is the image one; a video wait cannot survive a commit
	// either, and clearing the flag is what makes a late first frame of an older clip ignorable.
	band->setRequestIsVideo(false);
	if (policyBand)
		state.prefetchPath.clear();
	// The media that just became visible is pinned *before* the two paths it replaced are
	// re-evaluated: each syncPin() can run eviction, and at the configured floor the entry eviction
	// would then be free to take is exactly this one (M1).
	this->syncPin(media.path);
	// M3 Task 1: the media is recorded and written by the presenter, at the point applyDisplay()
	// used to write it -- after the mirror and the fill mode, so the commit's write order is the
	// one the slot has always seen.
	this->presentMedia(state, side, media, bandIndex);
	this->syncPin(previousShown);
	this->syncPin(previousCandidate);

	// The next swap is armed after this turn: the swap path must not walk the mascot directory.
	if (policyBand && armCandidate)
		this->armSpeculation(side);

	// M2: the left panel is the one the pair follows, so when it commits an *image* there is no
	// video offset coming for the right panel to copy -- it was armed by a video pick that has
	// since degraded (its sink never appeared, its pool emptied). Dropped here rather than only in
	// onVideoFailed(), because a give-up downgrade never produces a failure to hear about. A video
	// commit is untouched: onVideoFirstFrame() reads the flag after this call.
	if (policyBand && side == MascotSide::Left && media.type == MascotMedia::Image)
		this->rightState.mirrorCopyPending = false;

	if (policyBand && state.highlightOnApply) {
		state.highlightOnApply = false;
		emit this->themeColorChanged(this->colorFor(side, true));
	}
}

void MascotController::holdCandidate(SideState& state, const MascotMedia& media)
{
	if (media.path.isEmpty())
		return;
	const QString previous = state.candidate.path;
	state.candidate = media;
	// Pinned as soon as it exists, and before the candidate it replaces is re-evaluated: a
	// candidate whose pixels were evicted before its swap would put a blank id in the slot, and
	// the unpin below can run eviction.
	this->syncPin(media.path);
	this->syncPin(previous);
}

void MascotController::armSpeculation(MascotSide side)
{
	// Queued rather than called: the pick walks the mascot directory and posts a decode, and
	// neither may sit on the swap path (the swap must stay a property write). Queued to `this`,
	// so a stopped or destroyed controller simply never runs it; speculate() is idempotent, so
	// several commits in one turn collapse into one pick.
	QMetaObject::invokeMethod(this, [this, side] { this->speculate(this->stateFor(side), side); },
		Qt::QueuedConnection);
}

void MascotController::speculate(SideState& state, MascotSide side)
{
	if (!this->running || !state.surface)
		return;
	// Exactly one deep, per panel: while a candidate is held or its decode is in flight there is
	// nothing to do, and no queue is kept.
	if (!state.candidate.path.isEmpty() || !state.prefetchPath.isEmpty())
		return;
	// With the mirror key on, the right panel always shows what the left panel shows, so a
	// speculative pick for it would spend a draw nobody displays (and would shift a seeded
	// sequence). It arms itself again the moment it is updated on its own.
	if (side == MascotSide::Right && this->mirrorEnabled())
		return;
	// M2: a video panel has no use for a decoded image. Holding one would cost a decode, a cache
	// entry and a pin per clip, and pickFor() would find it waiting when the panel's type
	// changed back -- so the speculation is not armed at all while the panel shows video.
	if (this->pickType(state) == MascotMedia::Video)
		return;
	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return;
	state.prefetchPath = path;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

// ============================================================================================
// M2: the video branch. Everything below serves the same model the image branch serves -- one
// panel, one media, one commit -- and only exists because a video's media, cadence, players and
// failure modes are its own.
// ============================================================================================

bool MascotController::mediaSettingWantsVideo() const
{
	return this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0
		|| this->mediaSetting.compare(QStringLiteral("Mixed"), Qt::CaseInsensitive) == 0;
}

bool MascotController::mediaMayBeVideo() const
{
	// Both halves are needed: a budget below two players leaves no panel a player at all, and an
	// images-only setting has no use for the pool. Everything video costs hangs off this answer.
	return this->videoMode != VideoMode::Off && this->mediaSettingWantsVideo();
}

void MascotController::invalidateVideoPoolIfStale()
{
	MainWindow* window = this->App ? this->App->mainWindow : nullptr;
	if (!window)
		return;
	// The pool caches one getVideos() scan, and exactly three things make that scan wrong: the
	// model it was built from being reset (videos added or removed), the filters it applied
	// changing, and the database it read switching. §7.8 names those three, so they are what this
	// fingerprint compares -- once per video pick at most, and never on the image path, because
	// pickType() answers Image before it gets here for an images-only configuration.
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
	// Dropped here rather than from MainWindow, so the invalidation costs no new entry point on
	// the window: the controller is the only consumer of the pool, so it is the only object that
	// needs to know the scan went stale.
	this->source.invalidateVideoPool();
}

MascotMedia::Type MascotController::pickType(SideState& state)
{
	if (!this->mediaMayBeVideo())
		return MascotMedia::Image;
	this->invalidateVideoPoolIfStale();
	// The size is what decides, and it answers by scanning the pool if that has not happened yet
	// (MascotMediaSource keeps the scan behind a mutable cache for exactly this call).
	const int pool = this->source.videoPoolSize();
	if (this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0 && pool <= 0) {
		// `videoPoolSize() == 0` means "no row passed the filters" *or* "the query failed", and
		// the two are not distinguishable from here: either way the honest answer is images, and
		// the line is rate-limited because it describes the configuration, not an event.
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastTypeLogMs == 0 || (now - state.lastTypeLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastTypeLogMs = now;
			this->logVideo(QStringLiteral("video pool is empty (0 candidates); showing images for now"));
		}
		return MascotMedia::Image;
	}
	// Mixed takes video whenever there is one to take: the setting is the user saying they want
	// both kinds, and the pool is the only thing that can say whether both exist.
	return pool > 0 ? MascotMedia::Video : MascotMedia::Image;
}

MascotController::VideoPick MascotController::pickVideo(SideState& state, MascotSide side,
	bool updateHighlight)
{
	MascotBand* band = state.band(0);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!slot)
		return VideoPick::Fallback;
	// One load in flight per panel, the video analogue of the image path's request-in-flight test:
	// a beat or a second timer tick must not supersede a clip that is still opening, and it must
	// not fall through to an image either -- the panel is showing video and is merely busy.
	// A preload in flight is not that case: it is the *next* clip being made ready, so a swap that
	// arrives while it is still opening may simply take over (swapTo() supersedes it) instead of
	// making the panel wait -- and waiting would stall the cadence, because the single-shot clip
	// timer only re-arms on a commit.
	if (slot->isBusy() && !slot->preloading())
		return VideoPick::Skip;
	// §7.13's preview rule, and the only place a new clip is *chosen*: while the tooltip is
	// playing, the pick waits. Nothing already on screen is disturbed -- the panel keeps its clip,
	// its players and its cadence-less state; only a start is held back -- and no decoder is
	// touched, so a tooltip that started mid-clip costs the panel nothing until the clip after it.
	// Deferred rather than skipped, for the reason the sink retry below is: the clip timer that
	// asked for this swap has already fired (it is single-shot and only a commit re-arms it), so a
	// pick that simply returned would leave the panel on its old clip until the beat moved it --
	// and under `mascots_clip_seconds > 0` the beat does not advance a video panel at all. The
	// re-ask is this same pick path at PREVIEW_DEFER_MS for as long as the tooltip plays, so no
	// path is drawn from the pool until the first retry that finds it silent -- one draw for the
	// swap the deferral belongs to.
	// (Task 6: PREVIEW_DEFER_MS is `mascots_busy_defer_ms` now; the interval below is that value.)
	// Checked before the sink test so a deferral costs none of the sink-retry budget.
	if (this->previewTooltipBusy()) {
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastPreviewDeferLogMs == 0
			|| (now - state.lastPreviewDeferLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastPreviewDeferLogMs = now;
			this->logVideo(QStringLiteral("%1 swap waits: a preview tooltip is playing").arg(sideLabel(side)));
		}
		// The same flag the sink retry sets, for the same reason: this panel is about to start a
		// load, so under the mirror key the other panel's copy has to be armed from that load's
		// first frame rather than left waiting for a commit it will not see on its own.
		state.videoPickRetryPending = true;
		QTimer::singleShot(this->busyDeferMs, this, [this, side]() {
			if (!this->running)
				return;
			SideState& panel = this->stateFor(side);
			MascotBand* panelBand = panel.band(0);
			MascotVideoSlot* panelVideo = panelBand ? panelBand->video() : nullptr;
			// A load that started in the meantime (a click, the other panel's pair) is newer than
			// this deferral: it owns the panel's next frame.
			// (Task 6: `isBusy()` is also true while a *preload* opens, and a preload is not a newer
			// swap -- it was armed right after the commit this deferral belongs to. Testing it alone
			// would let the deferral drop itself in that window, and the panel would then wait for a
			// beat it may never get. Same pairing as pickVideo()'s busy gate, for the same reason.)
			if (!panelBand || !panelVideo || panelBand->requestIsVideo()
				|| (panelVideo->isBusy() && !panelVideo->preloading()))
				return;
			this->pickFor(panel, side, true);
		});
		return VideoPick::Skip;
	}
	// The sink belongs to the delegate, so it does not exist until QML has created it. A load
	// started without one would fail against a live panel that is only a moment behind, so the
	// pick is retried rather than reported: the surface has just been told its slot count, and the
	// delegate it creates in answer to that is what owns the sink.
	// Task 6: the budget and the interval are `mascots_retry_attempts` / `mascots_retry_interval_ms`
	// (§7.11): a delegate is normally there by the time the surface has applied its slot count, so
	// this is the belt to that braces.
	MascotSurface* surface = state.surface.data();
	if (!surface || !surface->slotSink(0, 0)) {
		if (state.videoSinkRetries++ < this->retryAttempts) {
			if (!this->warnedVideoSink) {
				this->warnedVideoSink = true;
				this->logVideo(QStringLiteral("no video sink for the panel yet; retrying the pick"));
			}
			// Recorded for nextImages(): while the pick is waiting, this panel is still the one the
			// mirror pair follows, so the right panel's copy has to be armed from its firstFrame
			// rather than left waiting for a commit that the right panel never sees.
			state.videoPickRetryPending = true;
			QTimer::singleShot(this->retryIntervalMs, this, [this, side]() {
				if (!this->running)
					return;
				SideState& panel = this->stateFor(side);
				MascotBand* panelBand = panel.band(0);
				if (panelBand && panelBand->media().path.isEmpty())
					this->pickFor(panel, side, true);
			});
			return VideoPick::Skip;
		}
		// The delegate never appeared: images are the honest degrade, and the log says why once --
		// once per process, not once per episode, because the retry budget below is reset so a
		// delegate that arrives later gets its own. The counter is reset here so a recovered
		// delegate is not decided by an exhausted budget.
		state.videoPickRetryPending = false;
		state.videoSinkRetries = 0;
		if (!this->warnedVideoSinkGiveUp) {
			this->warnedVideoSinkGiveUp = true;
			this->logVideo(QStringLiteral("no video sink for the panel after %1 retries; showing images instead")
				.arg(this->retryAttempts));
		}
		return VideoPick::Fallback;
	}
	state.videoSinkRetries = 0;
	state.videoPickRetryPending = false;
	// The clip this swap will show may already be chosen and opened: a preload decides the next
	// path when the previous clip commits, so consuming it here is what makes the swap an opacity
	// change. Drawing again would skip the parked file and shift the seeded sequence by one draw per
	// swap, which is why the parked path wins over a fresh one and why nothing is re-validated
	// against the pool: the pool was read when the preload was made, by this same code path.
	const QString parkedPath = slot->preloadedPath();
	if (!parkedPath.isEmpty()) {
		const qint64 parkedOffsetMs = slot->preloadedOffsetMs();
		// A video pick owns the panel exactly as it does below, for the same reasons.
		if (!state.candidate.path.isEmpty()) {
			const QString dropped = state.candidate.path;
			state.candidate = MascotMedia();
			this->syncPin(dropped);
		}
		state.prefetchPath.clear();
		this->preparePick(state, updateHighlight);
		// The offset is passed rather than left as -1: it is the number that was actually seeked to,
		// and passing it is what lets the log print it and a mirrored pair agree on it.
		this->startVideoLoad(state, side, parkedPath, parkedOffsetMs);
		return VideoPick::Started;
	}
	// The source reads no config, so the seed and the weighted switch are pushed before the pick
	// rather than only when the settings are applied (a change made mid-run would otherwise land
	// one video pick late).
	this->applySeedSource();
	this->source.setWeightedChoice(this->weightedRandom);
	const QString path = this->source.nextVideoPath();
	if (path.isEmpty())
		return VideoPick::Fallback;
	// A video pick owns the panel: an image candidate held from an image-mode pick would be shown
	// over this clip the moment the panel's type came back, and a decode still in flight for it
	// would be a decode nobody ever sees.
	if (!state.candidate.path.isEmpty()) {
		const QString dropped = state.candidate.path;
		state.candidate = MascotMedia();
		this->syncPin(dropped);
	}
	state.prefetchPath.clear();
	this->preparePick(state, updateHighlight);
	this->startVideoLoad(state, side, path, -1);
	return VideoPick::Started;
}

void MascotController::startVideoLoad(SideState& state, MascotSide side, const QString& path,
	qint64 offsetMs)
{
	MascotBand* band = state.band(0);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!state.surface || !band || !slot || path.isEmpty())
		return;
	// The sink may be gone even though pickVideo() saw it: a mirror copy does not pick, it replays
	// the left panel's file and offset, and its own delegate may be a turn behind. A load started
	// without a sink fails against a live panel, so this exact load is retried -- the same file at
	// the same offset, because that offset is the pair's invariant and a fresh pick would choose a
	// different one. Nothing is recorded in the meantime, so the panel is not left "waiting" for a
	// frame no player will ever paint.
	// The sinks are bound as late as possible (Task 2's contract): they do not exist until QML has
	// created the delegate, and a binding made when the slot was created would have bound null for
	// the life of the panel.
	// (M3 Task 1: the band's ensureVideo() is that binding now, and it is what this gate asks --
	// false means the delegate has no sink yet, which is the retry case.)
	if (!band->ensureVideo(0)) {
		if (state.videoLoadRetries++ < this->retryAttempts) {
			this->logVideo(QStringLiteral("%1 has no video sink yet; retrying the same load")
				.arg(sideLabel(side)));
			// The same flag a retried pick sets, because from outside this is the same situation:
			// this panel is about to start a load, so a mirrored pair has to arm the other panel's
			// copy from that load's first frame.
			state.videoPickRetryPending = true;
			QTimer::singleShot(this->retryIntervalMs, this, [this, side, path, offsetMs]() {
				if (!this->running)
					return;
				SideState& panel = this->stateFor(side);
				MascotBand* panelBand = panel.band(0);
				if (panelBand && panelBand->video())
					this->startVideoLoad(panel, side, path, offsetMs);
			});
			return;
		}
		state.videoLoadRetries = 0;
		state.videoPickRetryPending = false;
		// A parked clip this panel cannot start is not going to become startable: it is dropped so
		// the next swap draws a fresh one instead of retrying a clip whose output has no sink.
		slot->discardPreload();
		// Latched like the pick branch above: one copy per left commit would otherwise repeat this
		// once per clip for as long as the right panel's delegate is missing.
		if (!this->warnedVideoSinkGiveUp) {
			this->warnedVideoSinkGiveUp = true;
			this->logVideo(QStringLiteral("%1 could not start its load (%2 retries, no sink); the next swap re-pairs the panels")
				.arg(sideLabel(side))
				.arg(this->retryAttempts));
		}
		return;
	}
	state.videoLoadRetries = 0;
	state.videoPickRetryPending = false;
	// §7.5's ordering, ruled into this task: the output is made visible *before* the clip is asked
	// for, at the opacity it already carries (0 on a fresh panel), so the sink is receiving frames
	// while the first-frame gate waits. Making it visible on the frame instead would have the gate
	// waiting on an output that was not being drawn yet.
	if (!band->videoOutputVisible())
		band->setVideoOutputVisible(true);
	// Was this clip already chosen, opened and parked? If so the swap is the slot's play() plus the
	// fade below -- no load, and the offset that was actually seeked to is the slot's, not a value
	// this call would have had to invent. A preload that is not for exactly this file *and this
	// offset* is a miss (a superseding pick, a mirror copy whose parked clip was dropped, or the
	// pair's two panels parked on different moments of one file) and falls through to swapTo(),
	// which is the path that existed before preload did. The offset is part of the match for the
	// same reason it is part of preload()'s: a parked clip is a moment, so serving a swap that
	// asked for a different moment would put the pair on two different frames of one file.
	const bool fromPreload = slot->preloadedPath() == path && slot->hasPreload()
		&& (offsetMs < 0 || slot->preloadedOffsetMs() == offsetMs);
	const qint64 effectiveOffsetMs = fromPreload ? slot->preloadedOffsetMs() : offsetMs;
	band->setRequestPath(path);
	band->setRequestIsVideo(true);
	band->setWantedOffsetMs(effectiveOffsetMs);
	state.requestedAtMs = QDateTime::currentMSecsSinceEpoch();
	++state.videoSeq;
	// A left-panel video load is what the pair follows, so the right panel's copy is armed here
	// rather than only in nextImages(): a swap served from a preload completes *inside*
	// pickVideo(), which is before nextImages() could have armed anything, and the pair would then
	// never be copied. Arming it at the start of every left video load covers both timings with one
	// rule -- from this moment the right panel owes a copy of this panel's next commit.
	if (side == MascotSide::Left && this->mirrorEnabled())
		this->rightState.mirrorCopyPending = true;
	// The `preloaded=` field is how the log tells the two apart: a swap served from a parked clip
	// prints the offset it was parked at and can be paired with the "preloaded" line that preceded
	// it, while `preloaded=no` is a swap that paid for its own load.
	this->logVideo(QStringLiteral("%1 swap #%2 pool=%3 offset=%4 path=\"%5\" preloaded=%6")
		.arg(sideLabel(side))
		.arg(state.videoSeq)
		.arg(this->source.videoPoolSize())
		.arg(effectiveOffsetMs < 0 ? QStringLiteral("random") : QString::number(effectiveOffsetMs))
		.arg(path)
		.arg(fromPreload ? QStringLiteral("yes") : QStringLiteral("no")));
	if (fromPreload && slot->swapToPreloaded())
		return;
	// Nothing parked, or the parked clip could not be activated: the load this path has always done.
	// Silent about the miss -- a preload is an optimisation, and swapTo() is the behaviour the panel
	// had before it, so a miss is not a failure of anything.
	slot->swapTo(path, offsetMs);
}

void MascotController::onVideoFirstFrame(MascotSide side, const QString& path, int ab,
	const QSize& frameSize, qint64 offsetMs)
{
	if (!this->running)
		return;
	SideState& state = this->stateFor(side);
	MascotBand* band = state.band(0);
	if (!state.surface || !band || !band->video())
		return;
	// Only the load this panel is waiting for may be committed: a superseded load's frame (a swap
	// that arrived mid-load) is not this panel's media, exactly as a late image delivery is not
	// (amendment H1).
	if (!band->requestIsVideo() || band->requestPath() != path)
		return;

	const qint64 delta = QDateTime::currentMSecsSinceEpoch() - state.requestedAtMs;
	MascotMedia media;
	media.type = MascotMedia::Video;
	media.path = path;
	media.nativeSize = frameSize;
	media.startOffsetMs = offsetMs;

	state.videoFailures = 0;
	// Committed through the same call an image takes, which is what keeps the pins, the mirror
	// reset, highlightOnApply and the theme-colour emission working without a second copy of any
	// of them. `armCandidate` is false: a video panel has no image candidate to arm.
	this->showMedia(state, side, media, false, false, 0);
	this->fadeInVideo(state, side, ab);
	this->restartClipTimer(state, side);
	this->logVideo(QStringLiteral("%1 first frame #%2 ab=%3 %4x%5 offset=%6ms delta=%7ms path=\"%8\"")
		.arg(sideLabel(side))
		.arg(state.videoSeq)
		.arg(ab)
		.arg(frameSize.width())
		.arg(frameSize.height())
		.arg(offsetMs)
		.arg(delta)
		.arg(path));

	// A mirror pair shares the file *and* the offset, and the left panel's frame is where the
	// offset finally exists -- so this is the moment the right panel can be started on it.
	if (side == MascotSide::Left && this->rightState.mirrorCopyPending) {
		this->rightState.mirrorCopyPending = false;
		MascotBand* rightBand = this->rightState.band(0);
		if (this->rightState.surface && rightBand && rightBand->video())
			this->startMirrorVideo(path, offsetMs);
	}
}

void MascotController::fadeInVideo(SideState& state, MascotSide side, int ab)
{
	MascotBand* band = state.band(0);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!state.surface || !band || !slot || ab < 0 || ab > 1)
		return;
	// A crossfade needs two players. In the clamped mode a swap reloads the output that is on
	// screen, so there is nothing to blend and the duration is 0 -- the hard cut the clamp
	// promises -- rather than a second fade path to keep in step with the first.
	const int fade = this->videoMode == VideoMode::Crossfade ? this->crossfadeMs : 0;
	band->setVideoOpacity(ab, 1.0, fade);
	band->setVideoOpacity(1 - ab, 0.0, fade);
	if (fade <= 0) {
		// No fade to wait for, so the outgoing player's decoder is worth nothing: retired now.
		slot->retireInactive();
		// And only now, with the outgoing player retired, is the idle output the one the *next*
		// swap will use -- so this is the earliest moment a preload can be parked on the right
		// player. Parked before the retire it would be thrown away by it.
		this->preloadNext(side);
		return;
	}
	// The retirement ends the crossfade: until then the outgoing player is what is still partly
	// visible, which is the only reason its decoder is still open. Queued on `this`, so a
	// controller that was stopped in between simply never runs it.
	QTimer::singleShot(fade, this, [this, side]() {
		SideState& panel = this->stateFor(side);
		MascotBand* panelBand = panel.band(0);
		if (MascotVideoSlot* panelSlot = panelBand ? panelBand->video() : nullptr)
			panelSlot->retireInactive();
		// The same ordering rule as the hard cut above: the preload follows the retirement, because
		// until it has happened the output a preload would pick is the outgoing clip's.
		this->preloadNext(side);
	});
}

void MascotController::preloadNext(MascotSide side)
{
	// Queued for the same reason armSpeculation() is: the pick reads the pool and the preload posts
	// a load, and neither may sit inside the commit or the fade callback that asked for them. Queued
	// to `this`, so a controller that was stopped or destroyed in between simply never runs it.
	QMetaObject::invokeMethod(this, [this, side] { this->startPreload(side); }, Qt::QueuedConnection);
}

void MascotController::startPreload(MascotSide side)
{
	if (!this->running || !this->preloadEnabled || !this->mediaMayBeVideo())
		return;
	SideState& state = this->stateFor(side);
	MascotSurface* surface = state.surface.data();
	MascotBand* band = state.band(0);
	MascotVideoSlot* slot = band ? band->video() : nullptr;
	if (!surface || !band || !slot)
		return;
	// Never while the panel is paused-hidden: a preload opens a decoder and paints a frame for a
	// panel nobody can see, which is exactly the cost the pause rule (§7.13) exists to remove. The
	// resume path arms a fresh preload, so nothing is lost by waiting.
	if (this->videoPaused || !surface->isVisible())
		return;
	// Only a panel that is showing video has a next clip to prepare. An image panel's next media is
	// already speculative through armSpeculation(), which is the same trick for the other media.
	if (band->media().type != MascotMedia::Video || band->media().path.isEmpty())
		return;
	// A swap in flight owns the panel; a parked clip means one is already armed, and a preload in
	// flight is one being made. The first of those must never be disturbed, and the other two would
	// only repeat work.
	if (slot->isBusy() || slot->hasPreload())
		return;
	// Under the mirror key the pair shows one file at one offset, so the pick is made once, by the
	// left panel: the right panel's preload is started from the left's preloaded() report, which is
	// where the offset it has to match exists. A right panel that drew its own file here would
	// desync the pair -- the rule startMirrorVideo() enforces for swaps, applied one step earlier.
	if (side == MascotSide::Right && this->mirrorEnabled())
		return;
	// A missing sink is the delegate being a turn behind. The swap path has its own retry budget for
	// exactly that, so a preload does not compete for it: it simply waits for the next commit.
	// (M3 Task 1: the band's ensureVideo() also binds the sinks on the success path, which is the
	// setSinks() this method used to make just before preload().)
	if (!band->ensureVideo(0))
		return;
	// Which branch serves this pick is the same question the swap asks, and the pool it asks about
	// is the pool this preload will draw from: a panel whose type is not video has nothing to park.
	if (this->pickType(state) != MascotMedia::Video)
		return;
	// The seed and the weighted switch are pushed here for the same reason pickVideo() pushes them:
	// the source reads no config, and the clip chosen at preload time is chosen with whatever the
	// settings say *now* rather than at the next apply.
	this->applySeedSource();
	this->source.setWeightedChoice(this->weightedRandom);
	const QString path = this->source.nextVideoPath();
	if (path.isEmpty())
		return;
	// -1 asks the slot for the same start offset swapTo() would have chosen, so a preloaded clip
	// and a loaded one are indistinguishable in where they begin.
	slot->preload(path, -1);
}

void MascotController::onVideoPreloaded(MascotSide side, const QString& path, qint64 offsetMs,
	const QSize& frameSize)
{
	if (!this->running || path.isEmpty())
		return;
	SideState& state = this->stateFor(side);
	MascotBand* band = state.band(0);
	if (!state.surface || !band || !band->video())
		return;
	// The gate's evidence that a swap was served from a preload is this line followed by a
	// `swap #n ... preloaded=yes` for the same path and offset.
	this->logVideo(QStringLiteral("%1 preloaded path=\"%2\" offset=%3ms %4x%5")
		.arg(sideLabel(side))
		.arg(path)
		.arg(offsetMs)
		.arg(frameSize.width())
		.arg(frameSize.height()));
	// The pair shows one file at one offset, and the offset exists only now that the left panel has
	// reported it -- so the right panel's copy of the pair is parked here rather than chosen by its
	// own preload. Both panels are then parked on the same frame, and the swap is two flips in the
	// same turn instead of one panel waiting for the other's first frame.
	if (side != MascotSide::Left || !this->mirrorEnabled())
		return;
	SideState& right = this->rightState;
	MascotBand* rightBand = right.band(0);
	MascotVideoSlot* rightVideo = rightBand ? rightBand->video() : nullptr;
	if (!right.surface || !rightBand || !rightVideo)
		return;
	// The right panel is a turn behind (its own swap still opening) or already has a clip parked:
	// either way there is nothing to add here, and the next commit asks again.
	if (rightVideo->isBusy() || rightVideo->hasPreload())
		return;
	// Same call as the left panel's sink gate: bind the pair's two sinks before parking on them.
	if (!rightBand->ensureVideo(0))
		return;
	rightVideo->preload(path, offsetMs);
}

void MascotController::startMirrorVideo(const QString& path, qint64 offsetMs)
{
	SideState& right = this->rightState;
	MascotBand* rightBand = right.band(0);
	if (!right.surface || !rightBand || !rightBand->video() || path.isEmpty())
		return;
	// The pair shows one media, so the right panel's pick *is* the left panel's: same file, same
	// offset. A second random start would show two different moments of the same clip, which is
	// the one thing the mirror key exists to prevent.
	this->preparePick(right, false);
	this->startVideoLoad(right, MascotSide::Right, path, offsetMs);
}

void MascotController::onVideoFailed(MascotSide side, const QString& path,
	MascotVideoSlot::FailKind kind, bool timedOut)
{
	if (!this->running)
		return;
	SideState& state = this->stateFor(side);
	MascotBand* band = state.band(0);
	if (!state.surface || !band || !band->video())
		return;
	// §7.8: "failed files are dropped for the session". Recorded here because this is where a
	// failure is *known* -- a load the backend refused, a load that never painted, or a clip that
	// died on screen -- and the drop is applied by the only thing that draws (the source). Without
	// it the same unusable file comes back roughly every 1/pool swaps and costs a failure line or
	// five seconds of frozen last frame each time, which is the cost this rule exists to remove.
	// (Task 6: "five seconds" is the shipped default of `mascots_swap_timeout_ms`, i.e. whatever the
	// user set it to; the cost this rule removes is the same either way.)
	//
	// Only a *decode* failure may make that statement, because it is a statement about the file:
	// the backend refused the media and will refuse it again. The other two ways a load can end say
	// nothing about the file and must not cost it its place in the pool for the rest of the
	// session:
	//   - a timeout is a deadline, and a perfectly good file on a slow machine reaches it (the
	//     configured `mascots_swap_timeout_ms` is the user's judgement of their own machine, not a
	//     verdict on the media). Dropping it there would turn one slow open into a file the user
	//     never sees again until they restart.
	//   - a wiring report (no sink yet, the slot already shut down) is a fact about the player or
	//     the panel: nothing was ever attempted, so the file has not been tried, let alone failed.
	// Preload makes both of those ordinary rather than exotic: it opens a file before anything asks
	// for it, so it can meet a delegate that is a turn behind and it can reach the deadline, and
	// neither may take a file out of the rotation.
	// An empty path is a wiring report rather than a file, and the source ignores it.
	if (!timedOut && kind == MascotVideoSlot::FailKind::Decode)
		this->source.noteVideoFailure(path);
	// The panel keeps whatever it is drawing. A load that failed says nothing about the clip that
	// is on screen, and a clip that died on screen has already stopped and released itself (Task
	// 2 releases before emitting), so the frame it painted last is still in the slot: blanking
	// here would turn one bad file into a black panel.
	const bool wasLoading = band->requestIsVideo() && band->requestPath() == path;
	if (wasLoading) {
		band->setRequestIsVideo(false);
		band->setRequestPath(QString());
		band->setWantedOffsetMs(-1);
		++state.videoFailures;
	}
	// The other panel may be waiting for this panel's offset to start its copy of the pair. It
	// will never arrive now, and the next committed media re-pairs them.
	// Cleared whether or not this failure was the left panel's own swap: a left panel that just
	// failed anything is not going to commit a pair the right panel could copy, and leaving the
	// flag set would park a copy that waits for a report which is not coming.
	if (side == MascotSide::Left) {
		this->rightState.mirrorCopyPending = false;
		if (wasLoading)
			this->logVideo(QStringLiteral("the mirrored pair is unpaired for now; the next swap re-pairs it"));
	}
	// One line per unusable file, so a broken clip cannot print once per swap. A timeout is
	// reported separately because it means the file opened and never painted, which is a
	// different fault from "the backend refused it".
	// Task 6: the kind is printed as well, because it is what decides whether the path was dropped
	// for the session (see the rule above) -- a reader of the log has to be able to tell a wiring
	// report from a decode failure without inferring it.
	const QString kindLabel = timedOut ? QStringLiteral("timeout")
		: (kind == MascotVideoSlot::FailKind::Decode ? QStringLiteral("decode") : QStringLiteral("wiring"));
	const QString key = path + QStringLiteral("|") + kindLabel;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool repeat = state.lastFailedVideoPath == key
		&& (now - state.lastFailedVideoLogMs) < VIDEO_LOG_INTERVAL_MS;
	if (!repeat) {
		state.lastFailedVideoPath = key;
		state.lastFailedVideoLogMs = now;
		this->logVideo(QStringLiteral("%1 %2 #%3 onScreen=%4 path=\"%5\" kind=%6")
			.arg(sideLabel(side))
			.arg(timedOut ? QStringLiteral("timed out") : QStringLiteral("failed"))
			.arg(state.videoSeq)
			.arg(band->media().path.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
			.arg(path)
			.arg(kindLabel));
	}
	// Keep drawing what the panel has, and arm a replacement. A panel that is drawing something is
	// advanced by its clip timer or by the beat, so nothing more is needed; a panel that is drawing
	// nothing would have nothing to advance it, so it asks for a replacement itself -- queued, so
	// this never recurses out of a player's signal, and bounded, so a library the backend cannot
	// decode cannot be walked through at load speed.
	if (!band->media().path.isEmpty()) {
		return;
	}
	if (state.videoFailures > VIDEO_FAILURE_LIMIT) {
		if (state.videoFailures == VIDEO_FAILURE_LIMIT + 1) {
			this->logVideo(QStringLiteral("%1 gave up after %2 failures in a row; the next beat or clip timer tries again")
				.arg(sideLabel(side))
				.arg(VIDEO_FAILURE_LIMIT));
		}
		return;
	}
	QMetaObject::invokeMethod(this, [this, side]() {
		if (!this->running)
			return;
		SideState& panel = this->stateFor(side);
		MascotBand* panelBand = panel.band(0);
		if (!panel.surface || !panelBand || !panelBand->media().path.isEmpty())
			return;
		this->pickFor(panel, side, true);
	}, Qt::QueuedConnection);
}

bool MascotController::ensureVideoSlots()
{
	const bool wantVideo = this->mediaMayBeVideo();
	bool downgraded = false;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface)
			continue;
		// M3 Task 2: the panel's policy band only. A `Stripes` panel's extra bands get no player in
		// this task -- that is Task 5's per-band decoder budget -- and they show images instead, by
		// design (fillEmptyBands() says so where the fill happens). Once Task 5 generalises this
		// loop over `state.bands` inside the budget, the calls below are already per band.
		MascotBand* band = state.band(0);
		if (!band)
			continue;
		if (wantVideo && !band->video()) {
			this->createVideoSlot(state, side);
		}
		else if (!wantVideo && band->video()) {
			// The setting is images-only, or the budget cannot carry a panel: the players go.
			if (state.clipTimer)
				state.clipTimer->stop();
			// A clip that was on screen has no player behind it any more, so `shown` must stop
			// saying it is one. That claim is what onSwapBeat() reads to decide a panel's cadence:
			// a panel still claiming video would be skipped by the beat *and* have no clip timer,
			// i.e. frozen on a frame nothing owns until the app is restarted. The output is left
			// visible, so the QML VideoOutput keeps its last frame on screen until the first image
			// lands -- that commit hides it through applyDisplay() like any other image.
			// (M3 Task 1: `shown` is the band's `media()`, and forgetMedia() is what drops the
			// claim without touching the slot -- the output keeps its frame for the reason above.)
			if (band->media().type == MascotMedia::Video) {
				const QString previousShown = band->media().path;
				band->forgetMedia();
				// The owner field changed, so the pin invariant is re-established the way every
				// other path does it (a video path was never in the image cache, so this is a
				// no-op that keeps the rule uniform).
				this->syncPin(previousShown);
				downgraded = true;
			}
			this->destroyVideoSlot(state);
		}
		if (band->video())
			band->video()->setSinglePlayer(this->videoMode != VideoMode::Crossfade);
	}
	return downgraded;
}

void MascotController::createVideoSlot(SideState& state, MascotSide side)
{
	MascotBand* band = state.band(0);
	if (!state.surface || !band || band->video())
		return;
	// A child of this controller, not of the surface: releaseSurface() destroys it *before* the
	// surface, because the sinks it draws into belong to the surface's QML delegate.
	// (M3 Task 1: a child of the band now, which the controller owns through SideState. The
	// destruction order the sentence above describes is unchanged: destroyVideo() still runs from
	// releaseSurface() before the surface is deleted.)
	band->ensureVideo(0);
	MascotVideoSlot* slot = band->video();
	if (!slot)
		return;
	// The decoder budget and the clip rules are applied here as well as when they change, so a
	// slot created later (an Images -> Videos settings apply) starts from the current
	// configuration instead of from the class defaults.
	slot->setSinglePlayer(this->videoMode != VideoMode::Crossfade);
	slot->setRandomStart(this->randomStart, this->clipSeconds);
	// The waits come from config, not from the slot's defaults: a slot created after an Apply has to
	// start from the configured values, or a panel enabled later would use a deadline the user had
	// already changed.
	slot->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
	connect(slot, &MascotVideoSlot::firstFrame, this,
		[this, side](const QString& path, int ab, const QSize& frameSize, qint64 offsetMs) {
			this->onVideoFirstFrame(side, path, ab, frameSize, offsetMs);
		});
	connect(slot, &MascotVideoSlot::failed, this,
		[this, side](const QString& path, MascotVideoSlot::FailKind kind) {
			this->onVideoFailed(side, path, kind, false);
		});
	connect(slot, &MascotVideoSlot::timedOut, this,
		[this, side](const QString& path) {
			// A deadline passed. `kind` is only read when the log words the line, and the ruled
			// rule never drops a file for a timeout, so the shipped value is the honest one.
			this->onVideoFailed(side, path, MascotVideoSlot::FailKind::Decode, true);
		});
	// A parked clip is not a commit: nothing of it is on screen, so the only thing the controller
	// does with the report is start the mirror pair's copy of the same file at the same offset.
	connect(slot, &MascotVideoSlot::preloaded, this,
		[this, side](const QString& path, qint64 offsetMs, const QSize& frameSize) {
			this->onVideoPreloaded(side, path, offsetMs, frameSize);
		});
	// A panel built while the window cannot be seen starts paused, exactly like one that was
	// already running when the window went away.
	slot->setPaused(this->videoPaused);
}

void MascotController::destroyVideoSlot(SideState& state)
{
	// M3 Task 2: every band of the panel, not only its policy band. This is the method
	// releaseSurface() and stop() reach for through releaseSurface(), so it is the one place the
	// carried-forward Task 1 finding is closed: no band's players may outlive the surface whose
	// QML delegates own their sinks. A band with no players (the extra bands of a `Stripes` panel
	// in this task) is skipped by the guard inside destroyVideo().
	// Synchronous, like the surface: the players are detached from the sinks they draw into (which
	// still exist at this point) and then deleted.
	for (auto& owned : state.bands) {
		if (!owned->video())
			continue;
		owned->destroyVideo();
		owned->setRequestIsVideo(false);
		owned->setWantedOffsetMs(-1);
	}
	state.mirrorCopyPending = false;
	state.videoPickRetryPending = false;
	state.videoLoadRetries = 0;
}

void MascotController::applyDecoderBudget()
{
	// `mascots_max_decoders` is a hard ceiling on live players. The full design wants two per panel
	// -- one fades in while the other fades out -- and the clamp below it degrades rather than
	// failing: one player per panel with a hard cut, then no video at all.
	const int crossfadeWant = PLAYERS_PER_PANEL * VIDEO_PANEL_COUNT;
	VideoMode mode;
	if (this->maxDecoders >= crossfadeWant)
		mode = VideoMode::Crossfade;
	else if (this->maxDecoders >= VIDEO_PANEL_COUNT)
		mode = VideoMode::SinglePlayer;
	else
		mode = VideoMode::Off;
	this->videoMode = mode;
	// One line per budget rather than per Apply: the clamp is a statement about the configuration,
	// and the gate needs to read which mode a run was in. Nothing is written for an images-only
	// setting, so that run's log stays what merged master's was.
	if (this->loggedDecoderBudget == this->maxDecoders || !this->mediaSettingWantsVideo())
		return;
	this->loggedDecoderBudget = this->maxDecoders;
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
}

void MascotController::restartClipTimer(SideState& state, MascotSide side)
{
	if (!state.clipTimer) {
		// One timer per panel, owned by the controller so it survives the enable/disable cycles the
		// surfaces do not, and single-shot because every tick is answered by a fresh commit that
		// re-arms it.
		state.clipTimer = new QTimer(this);
		state.clipTimer->setSingleShot(true);
		connect(state.clipTimer, &QTimer::timeout, this, [this, side]() { this->onClipTimer(side); });
	}
	// 0 disables the timer entirely: the clip loops and the panel advances only on the beat.
	if (this->clipSeconds <= 0 || this->videoPaused) {
		state.clipTimer->stop();
		return;
	}
	// Under the mirror key the pair shows one media, so one timer advances it: the left panel's.
	// Two timers armed a few milliseconds apart would each ask for a swap and the second would
	// supersede the first -- two loads per clip.
	if (this->mirrorEnabled() && side == MascotSide::Right) {
		state.clipTimer->stop();
		return;
	}
	state.clipTimer->start(std::min(this->clipSeconds * 1000, MAX_CLIP_TIMER_MS));
}

void MascotController::onClipTimer(MascotSide side)
{
	if (!this->running)
		return;
	SideState& state = this->stateFor(side);
	MascotBand* band = state.band(0);
	if (!band || !band->video() || band->media().type != MascotMedia::Video)
		return;
	// The panel that timed out advances. Under the mirror key the pair advances together, which
	// only the left panel's timer ever asks for (restartClipTimer keeps the right one stopped).
	if (this->mirrorEnabled()) {
		this->nextImages(false);
		return;
	}
	this->nextImage(side, true);
}

void MascotController::stopClipTimers()
{
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (state->clipTimer)
			state->clipTimer->stop();
	}
}

bool MascotController::videoShouldPause() const
{
	if (!this->pauseUnfocused)
		return false;
	QWidget* window = this->watchedWindow.data();
	if (!window)
		return false;
	// Hidden and minimized are both "cannot be seen", and Qt calls a minimized window visible, so
	// the two are tested separately. Activation is the third way a panel goes unseen, and it is
	// only consulted after the window has been active once: before that, "not active" is the
	// startup state of a window on its way up rather than the user having switched away.
	if (!window->isVisible() || window->isMinimized())
		return true;
	if (this->sawWindowActivation && !window->isActiveWindow())
		return true;
	return false;
}

bool MascotController::previewTooltipBusy() const
{
	// §7.13: "no new clip starts while a preview tooltip is playing; bands already playing
	// continue." The tooltip is MainWindow's own popup (a QPointer, so a closed one is null here
	// rather than a flag this class would have to keep in step), and its controller's isPlaying()
	// is the honest answer to "is it playing" -- the popup's own visibility is not, because it is
	// shown before its first frame and destroyed after its last.
	// Asked of the preview, never imposed on it: nothing here pauses, seeks or sources it, and the
	// panels' own players are not touched either, which is the "already playing continues" half of
	// the clause.
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
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		MascotBand* band = state.band(0);
		MascotVideoSlot* slot = band ? band->video() : nullptr;
		if (slot)
			slot->setPaused(pause);
		if (!state.clipTimer) {
			// A panel that is being paused still has to give up a parked clip: the decoder it holds
			// is open for a panel nobody can see, which is the cost this rule removes. The resume
			// branch below arms a fresh one, so nothing is lost but the lead time.
			if (pause && slot)
				slot->discardPreload();
			continue;
		}
		if (pause) {
			// Nothing may advance while nothing can be seen: a timer that fired here would start a
			// load whose first frame is what the gate reads as "a swap happened".
			state.clipTimer->stop();
			// And the parked clip goes with the cadence: it is the next swap made ready, and a paused
			// panel has no next swap to make it ready for. Kept, it would hold a decoder open on a
			// panel nobody can see -- exactly what the pause rule exists to stop.
			if (slot)
				slot->discardPreload();
		}
		else if (band && band->media().type == MascotMedia::Video && !band->media().path.isEmpty()) {
			// The clip on screen is resumed by the player itself; its cadence starts again from here.
			this->restartClipTimer(state, side);
			// And the next clip is armed here rather than left to the next commit: the preload that
			// would have been parked was refused while the panel was hidden (a parked decoder is
			// exactly the cost the pause rule removes), so this is where the panel catches up -- one
			// clip of lead time before the timer that was just restarted asks for it.
			this->preloadNext(side);
		}
	}
	// Debug-gated like every line below it, and silent for an images-only setting: a pause that no
	// player observes is not something the gate should read, and the images run's log has to stay
	// what merged master's was.
	if (this->mediaSettingWantsVideo()) {
		this->logVideo(pause ? QStringLiteral("paused (the window cannot be seen)")
			: QStringLiteral("resumed (the window is visible and active)"));
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
	// A window that is already active when it is first watched has been activated: the startup
	// race the flag exists for (a window that is on its way up is briefly "not active") is over
	// before the first pause decision, so the deactivate rule applies from now on. Without this a
	// controller restarted while its window has focus would ignore the user switching away until
	// the next activation.
	if (window->isActiveWindow())
		this->sawWindowActivation = true;
}

void MascotController::logVideo(const QString& message) const
{
	// Debug-gated exactly like MainApp's MASCOT-CACHE line: the Logger writes to the log window
	// always and to OutputDebugString only in debug mode, so gating here means an ordinary run
	// writes nothing at all while the gate (which runs with -debug) reads every line. The tag is
	// in the message as well as in the Logger's type, because the gate reads the message only.
	if (!this->App || !this->App->debug_mode || !this->App->logger)
		return;
	this->App->logger->log(VIDEO_LOG_TAG + message, QStringLiteral("MASCOT-VIDEO"));
}

void MascotController::connectBeats()
{
	if (this->swapConnection)
		return;
	if (!this->App || !this->App->MascotsAnimation)
		return;
	// Amendment W: the controller takes the beat connections itself. MainApp's old handlers
	// were gated on the retired widgets' isVisible(), and those widgets were hidden after the
	// cutover, so a branch inside them could never run.
	this->swapConnection = connect(this->App->MascotsAnimation, &mascotsAnimationsThread::updateMascotsSignal,
		this, &MascotController::onSwapBeat);
	this->flipConnection = connect(this->App->MascotsAnimation, &mascotsAnimationsThread::updateMascotsAnimationSignal,
		this, &MascotController::onFlipBeat);
}

void MascotController::disconnectBeats()
{
	if (this->swapConnection)
		disconnect(this->swapConnection);
	if (this->flipConnection)
		disconnect(this->flipConnection);
	this->swapConnection = QMetaObject::Connection();
	this->flipConnection = QMetaObject::Connection();
}

void MascotController::onSwapBeat()
{
	if (!this->running || !this->beatsAllowed())
		return;
	// M2: a video panel's cadence is its clip timer, not the beat (§7.9's "the clip timer governs
	// video slots"). A beat arrives every `mascots_frequency` milliseconds, which is far shorter
	// than any clip length, so letting it advance a video panel would churn clips several times a
	// second and the clip timer would never be the thing that decided anything. The test is what a
	// panel is *showing*, not what the setting asks for: a panel that fell back to images is
	// cadenced like an image panel, so a library of unusable files cannot freeze it, and
	// `mascots_media=Images` keeps M1's cadence exactly because nothing there is ever a video.
	const MascotBand* leftBand = this->leftState.band(0);
	const MascotBand* rightBand = this->rightState.band(0);
	const bool leftVideo = leftBand && leftBand->media().type == MascotMedia::Video;
	const bool rightVideo = rightBand && rightBand->media().type == MascotMedia::Video;
	if (!leftVideo && !rightVideo) {
		// M1, untouched: the pair advances together and the mirror key decides whether the right
		// panel copies the left one.
		this->nextImages(false);
		return;
	}
	// `mascots_clip_seconds` = 0 is the one setting under which the beat *is* the video cadence
	// (the clip loops and only the beat moves it on), so the beat keeps working for a video panel
	// exactly as it did before this branch existed.
	const bool beatDrivesVideo = this->clipSeconds <= 0;
	if (this->mirrorEnabled()) {
		// The pair is one media, so it is advanced by one thing: its clip timer while that timer is
		// running, and the beat only when there is no timer at all.
		if (beatDrivesVideo)
			this->nextImages(false);
		return;
	}
	if (!leftVideo || beatDrivesVideo)
		this->nextImage(MascotSide::Left, true);
	if (!rightVideo || beatDrivesVideo)
		this->nextImage(MascotSide::Right, false);
}

void MascotController::onFlipBeat()
{
	if (!this->running || !this->beatsAllowed())
		return;
	this->flipBoth();
}

bool MascotController::beatsAllowed() const
{
	// MainApp's handlers used to test the old image widget's `isVisible()` together with
	// utils::IsMyWindowVisible(); the surface is that widget's replacement, so the same two
	// conditions apply to it.
	MascotSurface* surface = this->leftState.surface ? this->leftState.surface.data()
		: this->rightState.surface.data();
	if (!surface || !surface->isVisible())
		return false;
	QWidget* window = surface->window();
	return window && utils::IsMyWindowVisible(window);
}

void MascotController::onMediaReady(const MascotMedia& media)
{
	if (!this->running || media.path.isEmpty())
		return;
	// A media belongs to a panel only while that panel is waiting for exactly that path, either
	// as its own next swap (`requestedPath`) or as its speculative pick (`prefetchPath`). A
	// delivery nobody asked for (a republish for another panel, or one that arrived while
	// stopped) is dropped rather than shown, which is what makes a request a request and not a
	// promise (amendment H1).
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface)
			continue;
		// M3 Task 2: a delivery may be for any band of the panel, because a `Stripes` panel fills
		// its extra bands through this same request/delivery path. Every band's own request is a
		// request, so every band is asked -- the policy band first, so a panel whose band 0 is
		// waiting for the same media still gets it through the path it always did.
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.bands[static_cast<std::size_t>(i)].get();
			if (!band)
				continue;
			// M2: a video wait is not an image wait. The paths cannot collide (one is a mascot image,
			// the other a library video), but a delivery must never be applied to a panel that is
			// waiting for a player's frame, so the flag is checked rather than assumed.
			if (band->requestIsVideo())
				continue;
			if (band->requestPath() != media.path)
				continue;
			// showMedia() drops both waits: the media is on screen, so it is not also a candidate
			// for a later swap, and any older pick is stale.
			// (M3 Task 2: "both" is that one band's request and the panel's speculation, so the
			// drop stays per band; `armCandidate` is the policy band's only because a fill of an
			// extra band has nothing to speculate for.)
			this->showMedia(state, side, media, false, i == 0, i);
		}
		// The speculative half stays with band 0: `prefetchPath` and the candidate are one deep
		// *per panel* until Task 5/6 give the bands their own (M3 Task 2). A policy band that is
		// waiting for a video keeps its wait, exactly as it did before bands were counted.
		MascotBand* band = state.band(0);
		if (!band || band->requestIsVideo())
			continue;
		if (state.prefetchPath == media.path) {
			state.prefetchPath.clear();
			this->holdCandidate(state, media);
		}
	}
}

void MascotController::onMediaFailed(const QString& path)
{
	// Nothing arrives for this path, so nothing is applied: the panel keeps what it has and the
	// next swap re-rolls. Clearing both waits is what keeps a later successful delivery of the
	// same path from landing on a panel that has moved on, and what lets the next commit arm a
	// fresh candidate instead of waiting for one that will never come.
	for (SideState* state : { &this->leftState, &this->rightState }) {
		// M3 Task 2: every band, so an extra band whose fill failed is not left holding a request
		// that will never be satisfied (fillEmptyBands() reads that request to decide whether the
		// band already has content coming). A failed fill is not retried here: the panel's next
		// swap re-rolls band 0, and an extra band is re-filled by the next layout change -- the
		// per-band cadence that would retry it is Task 6's.
		for (auto& owned : state->bands) {
			MascotBand* band = owned.get();
			if (!band)
				continue;
			// M2: an image decode failing says nothing about the video a panel may be loading, so a
			// panel waiting for a player's frame keeps its wait.
			// (M3 Task 2: "a panel" is any band of it -- an extra band waiting for a video cannot
			// happen in this task, but the rule is per band either way.)
			if (band->requestIsVideo())
				continue;
			if (band->requestPath() == path)
				band->setRequestPath(QString());
		}
		if (state->prefetchPath == path)
			state->prefetchPath.clear();
	}
}
