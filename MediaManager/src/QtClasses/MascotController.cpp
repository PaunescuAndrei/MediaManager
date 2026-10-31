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
#include <QPalette>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
	// The tag every video line carries, in the message as well as in the Logger's type: the line
	// is read from OutputDebugString by the gate's harness, which only sees the message.
	const QString VIDEO_LOG_TAG = QStringLiteral("MASCOT-VIDEO: ");
	// M3 Task 4: the same tag arrangement for the scroll driver's lines. A separate prefix and a
	// separate Logger type so a run can be read for the pan alone, and so a config that never pans
	// logs nothing under it at all.
	const QString SCROLL_LOG_TAG = QStringLiteral("MASCOT-SCROLL: ");
	// M4 Task 2: the background layer's own tag, for the same reason the two above have one: the
	// allocation and the gating can be read apart from the columns' video lines, and a run with
	// `mascots_background` off writes nothing under it at all.
	const QString BACKGROUND_LOG_TAG = QStringLiteral("MASCOT-BG: ");

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

	// M4 Task 1: the same two words for the two pair panels, plus the third panel's own name for
	// the lines a per-panel function will write once the background has something to say. The pair
	// panels keep exactly the text they had, so a `Static` run's log stays unchanged.
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

	// M3 Task 6A: the panel a line is about, plus the band when it is not the policy band. Band 0
	// keeps exactly the text it had, which is what keeps a `Static` run's log unchanged; a
	// `Stripes` panel's extra bands are named so a swap line and its first-frame line still pair
	// up, and so two bands' lines cannot be mistaken for one band swapping twice.
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
	// (M3 Task 5: that argument is stripesStackVertically() now. `Horizontal` stripes and every
	// single-band mode stack top to bottom (`true`); `Vertical` stripes stack left to right
	// (`false`) and the branch below slices the panel's width instead of its height. The rounding
	// rule is the same on either axis, so the slices still sum to the panel exactly.)
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

	// ---- M4 Task 6: the background marquee's geometry ----
	// One cell of the seamless marquee: `count` cells of `cellLen = axisLen / (count - 1)` laid
	// along the axis, so the cells are one cell longer than the visible window. `total` is the
	// distance after which the pattern repeats; placing cell `index` at
	// `fmod(index * cellLen + offset, total) - cellLen` normalised into
	// [-cellLen, total - cellLen) is what makes the window `[0, axisLen)` covered exactly once at
	// every offset: the `-cellLen` shifts the leading gap the bare modulo would leave behind the
	// edge, where the cell that has just wrapped is waiting. The cell off the far edge is the wrap
	// buffer, so N cells need no extra decoder. `count` must be at least 2; a one-band background
	// is a static full-window cell and never reaches this function.
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

	// ---- M3 Task 4: the scroll driver's arithmetic ----
	// Everything below is pure geometry with no QML and no config in it, so the pan can be reasoned
	// about (and its wrap points counted) without a surface, a timer or a screenshot. The driver's
	// job is the four lines that turn these into one property write per band.

	// ~60 fps is what §7.6 asks for ("~16 ms"). A precise timer is the one Qt will not coalesce
	// into the coarse 15 ms/1 s buckets, which at 40 px/s would show as a stutter rather than a
	// pan -- and this timer runs on the GUI thread, so a coarse one would also make its wakeups
	// less predictable.
	constexpr int SCROLL_TICK_MS = 16;

	// `mascots_scroll_speed` is a px/s rate, clamped where it is read. The floor is 0 (the band
	// stands still) rather than the absolute value: a negative speed must not silently invert the
	// direction the user asked for, because `mascots_scroll_direction` is the only thing that
	// decides the sense. The ceiling is far above any speed a panel this size can show as motion
	// instead of a strobe, and it bounds the one multiplication a tick does.
	constexpr int MAX_SCROLL_SPEED = 2000;

	// The phase accumulates in travel lengths, and two of them make one pingpong cycle: the first
	// unit interval is the forward pass, the second the return leg. Keeping both kinds of seam in
	// one accumulator is what makes "a resize preserves the phase" one stored number per panel
	// instead of a phase per axis, per direction or per band.
	constexpr double SCROLL_PHASE_SPAN = 2.0;

	// One tick's advance of a phase, `delta` in travel lengths, wrapped with fmod. fmod and not a
	// clamp: a tick that missed (a drag, a slow frame, a suspended machine) must land somewhere
	// inside the pass rather than have the whole missed distance played back as a pan, which is
	// what "a stall cannot make a band jump by more than one travel" means for the drawn position.
	double advancedScrollPhase(double phase, double delta)
	{
		double next = std::fmod(phase + delta, SCROLL_PHASE_SPAN);
		if (next < 0.0)
			next += SCROLL_PHASE_SPAN;
		return next;
	}

	// The fraction of its own travel one band has covered: 0 is the content's near end (its left
	// edge flush with the band's left edge, or its top with the band's top) and 1 its far end.
	// `offset` is the band's share of the pass (Task 5's `index / count` stagger), in the same
	// travel-length units, so a staggered band is simply a band whose pass starts later. Wrap keeps
	// fmod's sawtooth -- the seam is the jump back to 0 -- and pingpong folds the second interval
	// back on itself, so the same phase gives a reflection instead of a jump.
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

	// The translate a fraction asks for, in pixels, on the band's pan axis. The surface centres the
	// content, so the pan runs from +travel/2 -- the content's near end flush with the band's near
	// edge -- to -travel/2, one whole travel: the spec's "0 -> -(contentWidth - panelWidth)" with
	// the centring the surface already applies added to both ends. `reverse` swaps the two ends and
	// leaves the span (and therefore the speed) alone.
	double scrollOffsetFor(double fraction, double travel, bool reverse)
	{
		const double travelled = reverse ? 1.0 - fraction : fraction;
		return (0.5 - travelled) * travel;
	}

	// `mascots_scroll_direction`'s resolved sense as the log spells it, so the line Ruling M3-4
	// asks for reads without knowing the enum.
	QString scrollSenseLabel(bool reverse)
	{
		return reverse ? QStringLiteral("right to left") : QStringLiteral("left to right");
	}

	QString scrollAxisLabel(bool vertical)
	{
		return vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal");
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

	// The cheap half of "a user can see this window": visible and not minimized, both of which are
	// Qt flags. The other half -- not covered by another top-level window -- is
	// utils::IsMyWindowVisible()'s IsMyWindowCovered(), which walks every top-level window and
	// builds and combines GDI regions. That is affordable at the beat cadence and at a settings
	// Apply, and not affordable 60 times a second, so a tick asks this and leaves the covered case
	// to the event-driven reconciliation (see scrollCanTick()).
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
	// M4 Task 2: the window-sized host the background layer is a child of. It is the window's
	// central widget, so it is the one rectangle every resize of the window goes through.
	this->backgroundHost = backgroundHost;

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
	this->configureSurface(this->leftState, MascotPanel::Left);
	this->configureSurface(this->rightState, MascotPanel::Right);
	// M5 Task 8: a pair built by an apply that ran before it existed would otherwise keep the QML
	// defaults -- no backdrop and full opacity -- however `mascots_columns_z` is set.
	this->refreshColumnsVeil();
	this->updateLayout();
}

void MascotController::start()
{
	// M4 Task 2: `running` alone is no longer "nothing to do". A controller the background keeps
	// alive can be missing its pair -- stopPanels() took the columns down while the layer went on
	// drawing -- and showMascots() reaches this same entry point to bring them back. So the old
	// idempotence is kept by "the pair is here", and a start() that finds it missing rebuilds it.
	const bool pairPresent = this->leftState.surface && this->rightState.surface;
	if (this->running && pairPresent)
		return;
	const bool alreadyRunning = this->running;
	// Config first: createSurfaces() applies it to the slots it builds.
	this->applyConfig();
	this->createSurfaces();
	if (!alreadyRunning) {
		this->running = true;
		// M4 Task 2: after `running`, because the background surface exists if and only if this
		// controller is running and `mascots_background` is on -- and applyConfig() above ran
		// while `running` was still false.
		this->syncBackgroundSurface();
		this->connectBeats();
	}
	// M2: the pause state is derived from the window the surfaces have just landed in, so a panel
	// enabled while the window is minimized or in the background starts paused instead of loading
	// a clip nobody can see.
	this->applyVideoPaused();
	// Both panels were empty when the old code ran updateMascots() right before
	// showMascots(); one seeding swap does both jobs now.
	// (M4 Task 2: only when the columns are on. A controller started for the background alone
	// must not walk the mascot directory for two panels nobody shows, and a start() that rebuilds
	// the columns after stopPanels() seeds them on exactly the path the first enable used.)
	if (this->columnsEnabled) {
		this->nextImages(false);
		// M3 Task 2: `nextImages()` seeds the band the panel's policy lives on; a `Stripes` panel has
		// more than one band, and the rest are filled here so an enabled Stripes panel is never half
		// blank. A one-band panel is asked for nothing at all -- it has already been seeded, and this
		// is the only place `Static` and `Scroll` could have paid an extra pick they never paid
		// before -- while in Stripes the loop's per-band guards skip the band that is already loading.
		if (this->bandCount(MascotPanel::Left) > 1 || this->bandCount(MascotPanel::Right) > 1)
			this->fillEmptyBands();
	}
	// M4 Task 2: the background's bands are seeded through their own video-only pick path, one
	// pick per band, when the layer was built. A zero-player background has no bands at all, so
	// this is also the case the zero-player line describes: nothing is drawn and nothing is asked.
	if (this->backgroundEnabled && this->backgroundState.surface) {
		const int count = static_cast<int>(this->backgroundState.bands.size());
		for (int i = 0; i < count; ++i) {
			MascotBand* band = this->backgroundState.band(i);
			// A band that is already loading or drawing is left alone: start() can also be the
			// entry point that brings the columns back under a running background, and re-seeding
			// the layer then would replace a clip that is already on screen.
			if (!band || !band->media().path.isEmpty() || !band->requestPath().isEmpty())
				continue;
			this->pickFor(this->backgroundState, MascotPanel::Background, i, i == 0);
		}
	}
	// M3 Task 4: the pan starts here. It is asked rather than assumed because the answer also
	// depends on the window the surfaces just landed in: a controller enabled while its window is
	// minimized starts with the driver stopped, exactly as it starts with the players paused.
	this->updateScrollDriver();
}

void MascotController::stop()
{
	this->running = false;
	// M3 Task 4: first, before anything below touches a surface or a band: a tick that arrived
	// after the surfaces were released would write into a slot state that is being destroyed.
	// Stopping the timer costs one comparison on a controller that never scrolled.
	this->stopScrollDriver();
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
	for (PanelState* state : { &this->leftState, &this->rightState }) {
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
	// M4 Task 2: the background goes through its own invariant, which is the only place it is
	// built or dropped. `running` is false by now, so this is precisely "tear the layer down":
	// its bands' players are shut down, the slots and pins are cleared, and the surface is
	// released -- the pair's order above, for the pair's reason.
	this->syncBackgroundSurface();
}

void MascotController::stopPanels()
{
	// M4 Task 2: the pair's half of stop(), leaving the background layer running. It is what
	// MainWindow::hideMascots() reaches for while `mascots_background` is on, because turning the
	// columns off must not kill the layer behind them; with the background off the window calls
	// stop() and gets the full teardown it always got.
	for (PanelState* state : { &this->leftState, &this->rightState }) {
		for (auto& owned : state->bands) {
			if (owned->video())
				owned->video()->shutdown();
		}
	}
	// The same "nothing may be applied after this point" clearing stop() does, so a decode still
	// in flight for a column cannot land in a slot that is being released. The background never
	// requests an image decode (it is video-only), so clearing the shared prefetcher costs it
	// nothing.
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->clear();
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
	this->releaseSurface(this->leftState);
	this->releaseSurface(this->rightState);
	// The pair may have been the only reason the driver was running; the background may be the
	// only reason it still is. Reconciled here rather than left to the next window event, so a
	// stopped column cannot leave a timer ticking for it.
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

	// Amendment D's `mascots_media` is read here so it is not a write-only key. M1 renders
	// images only; a value this milestone cannot honour is reported rather than ignored. Said
	// once per process: it is a statement about the configuration, not an event, so repeating it
	// on every Apply would only bury the rest of the log.
	// M2 honours all three values, so the warning below now covers only a value that is none of
	// them; the two sentences above stand as the record of what this line asserted in M1. The six
	// video keys are read in the same place and for the same reason -- this method is this
	// class's only config reader -- and pickType() decides from the cached values, so a settings
	// change cannot land one pick late and no swap path reads the config at all.
	// M3 Task 6A: the value the last Apply cached, so a change of media setting can be told apart
	// from an Apply that changed something else -- only the former has to re-fill bands that
	// already have content in the other media kind.
	// (M3 Task 6A fix: the cached previous value is gone with the media-changed entry point. The
	// re-fill is now the per-band one ensureVideoSlots() queues when a band is *given* a player,
	// which is exactly the transition an Images -> Videos apply makes; the other direction is the
	// downgrade/refill path that already existed. That leaves one content action per band instead
	// of three competing ones.)
	this->mediaSetting = config->get(QStringLiteral("mascots_media"));
	this->clipSeconds = clampedClipSeconds(config);
	// (M3 Task 6B: `mascots_swap_on_beat`, read here under the same "one reader" rule as every key
	// above so onBeat() decides from the cached value and no beat path re-reads the config.)
	this->swapOnBeat = config->get_bool(QStringLiteral("mascots_swap_on_beat"));
	// (M3 whole-branch fix: a change queued at a beat waits for that band's scroll seam, and a
	// seam can be a whole clip away -- long enough for this Apply to have turned
	// `mascots_swap_on_beat` off, changed the clip length, or left the panel in `Static` where
	// there is no seam at all. Such a commit would then fire against a cadence that no longer
	// asked for it, so whatever the old configuration queued is dropped here. The new
	// configuration queues its own on the next beat.)
	// (M4 Task 4: the background's bands queue the same commits -- onBeat() schedules them -- and
	// the same Apply can leave the layer in `Static`, where no seam will ever run. The layer is in
	// this walk too, so a change parked under the previous configuration cannot fire at a later
	// background seam.)
	for (PanelState* state : { &this->leftState, &this->rightState, &this->backgroundState }) {
		for (auto& owned : state->bands) {
			if (owned)
				owned->discardWrapCommit();
		}
	}
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
	// (M3 Task 5: both are read below now, under the same "one reader" rule: the orientation is
	// cached with the counts and the axis, fill mode and scroll direction all answer from it, so
	// a settings change cannot turn one of the three without the others.)
	bool modeOk = false;
	const QString modeValue = config->get(QStringLiteral("mascots_mode"));
	const MascotMode parsedMode = MascotController::modeFromString(modeValue, &modeOk);
	this->columnsLayout.mode = modeOk ? parsedMode : MascotMode::Static;
	this->stripesLeftBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_left"), 2, 1, 8);
	this->stripesRightBands = clampedConfigInt(config, QStringLiteral("mascots_stripes_right"), 2, 1, 8);
	// M3 Task 5: the stacking axis and the alternate flag, read here and cached for the reason the
	// counts above are. An unreadable orientation is not clamped into one of the two names (there
	// is no sensible floor between them): it falls back to `Horizontal`, the documented default,
	// and is reported by the latch below.
	bool orientationOk = false;
	const QString orientationValue = config->get(QStringLiteral("mascots_stripes_orientation"));
	const StripesOrientation parsedOrientation =
		MascotController::stripesOrientationFromString(orientationValue, &orientationOk);
	this->columnsLayout.orientation =
		orientationOk ? parsedOrientation : StripesOrientation::Horizontal;
	this->columnsLayout.alternate = config->get_bool(QStringLiteral("mascots_stripes_alternate"));
	// M3 Task 3: the fill mode. Read here under the same "one reader" rule as every key above:
	// effectiveFillMode() answers from this cached value, so a settings change cannot land one
	// repaint late and no repaint path re-reads the config. The documented default is
	// `HeightFit` -- what M1 shipped (the retired renderer's scaled(w, height, KeepAspectRatio))
	// and what every existing panel already draws -- so a value this class cannot honour must not
	// reshape a picture just because the key is absent or mistyped (Ruling M3-2).
	bool fillOk = false;
	const QString fillValue = config->get(QStringLiteral("mascots_fill"));
	const MascotSurface::FillMode parsedFill = MascotController::fillModeFromString(fillValue, &fillOk);
	this->columnsLayout.fill = fillOk ? parsedFill : MascotSurface::FillMode::HeightFit;

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

	// M3 Task 5: the same one-warning latch as the three blocks above, for the same reason -- an
	// unsupported value is a statement about the configuration, not an event. The fallback is
	// `Horizontal` rather than a guess: it is the documented default and the axis a Stripes panel
	// had before the key existed, so a typo leaves the layout alone instead of turning it on its
	// side. An empty value is not reported: that is the key simply not being written, which
	// `Config` answers with the default.
	if (!this->warnedUnsupportedStripesOrientation && !orientationValue.isEmpty() && !orientationOk) {
		this->warnedUnsupportedStripesOrientation = true;
		this->logVideo(QStringLiteral("mascots_stripes_orientation=\"%1\" is not one of Horizontal or Vertical; using Horizontal")
			.arg(orientationValue));
		// The ungated warning beside the debug line, exactly as the blocks above have it: a typo
		// here stacks every Stripe panel along an axis the user did not ask for.
		qWarning() << "mascots_stripes_orientation:" << orientationValue
			<< "is not one of Horizontal or Vertical; using Horizontal.";
	}

	// M3 Task 4: the scroll driver's three keys, read here under the same "one reader" rule as
	// every key above: the tick answers from these cached values, so a settings change cannot land
	// one frame late and no tick reads the config. `mascots_scroll_speed` is clamped in px/s with 0
	// as its floor -- a negative speed means "stand still", not "reverse", because the direction
	// key is the only thing that decides the sense (clampedConfigInt() applies the floor to a
	// number the user wrote and falls back to the documented default for one that does not parse).
	this->columnsLayout.scrollSpeed = clampedConfigInt(config, QStringLiteral("mascots_scroll_speed"), 40, 0, MAX_SCROLL_SPEED);
	this->scrollDirectionText = config->get(QStringLiteral("mascots_scroll_direction"));
	bool directionOk = false;
	const ScrollDirection parsedDirection =
		MascotController::scrollDirectionFromString(this->scrollDirectionText, &directionOk);
	this->columnsLayout.scrollDirection = directionOk ? parsedDirection : ScrollDirection::LeftToRight;
	this->columnsLayout.scrollPingpong = config->get_bool(QStringLiteral("mascots_scroll_pingpong"));

	// The same one-warning latch as the three blocks above, for the same reason. The fallback is
	// `LeftToRight`: a value this class cannot read must not stop a band from panning, and the
	// default is also what the mode's axis would give a vertical direction here (Ruling M3-4). An
	// empty value is not reported: that is the key simply not being written.
	if (!this->warnedUnsupportedScrollDirection && !this->scrollDirectionText.isEmpty() && !directionOk) {
		this->warnedUnsupportedScrollDirection = true;
		this->logScroll(QStringLiteral("mascots_scroll_direction=\"%1\" is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight")
			.arg(this->scrollDirectionText));
		// The ungated warning beside the debug line, exactly as the blocks above have it: a typo
		// here changes the sense every panning band travels in, and a debug-gated line is invisible
		// in the run where the user needs to see why.
		qWarning() << "mascots_scroll_direction:" << this->scrollDirectionText
			<< "is not one of LeftToRight, RightToLeft, TopToBottom or BottomToTop; using LeftToRight.";
	}

	// Ruling M3-4: a direction whose axis disagrees with the mode's pan axis keeps its *sense* and
	// takes the mode's axis -- the user asked for a band that scrolls, so the action is
	// unambiguous -- and says so once per configured value rather than once per tick. It is said
	// only while a mode with a pan in it is configured: a `Static` panel has no axis to disagree
	// with, and complaining about a key nothing reads would be noise.
	if (this->columnsLayout.mode != MascotMode::Static
		&& this->scrollDirectionIsVertical(MascotPanel::Left) != this->scrollPanIsVertical(MascotPanel::Left)
		&& this->loggedScrollDirection != this->scrollDirectionText) {
		this->loggedScrollDirection = this->scrollDirectionText;
		this->logScroll(QStringLiteral("mascots_scroll_direction=\"%1\" names a %2 direction but %3 pans %4; taking the mode's axis and travelling %5")
			.arg(this->scrollDirectionText,
				scrollAxisLabel(this->scrollDirectionIsVertical(MascotPanel::Left)),
				modeLabel(this->columnsLayout.mode),
				scrollAxisLabel(this->scrollPanIsVertical(MascotPanel::Left)),
				scrollSenseLabel(this->scrollDirectionReverses(MascotPanel::Left))));
	}

	// ---- M4 Task 1: the background layout ----
	// (The background surface that consumes these values is added later in this milestone. They are
	// parsed here so the keys have exactly one reader and the background cannot pick up its layout
	// one Apply late; nothing below draws from them yet. The background's applied latches stay at
	// their defaults until Task 2 gives it a surface.)
	// (M4 Task 2: the surface exists now, and these are the values it draws from. `mascots` is read
	// beside them because start() has to know which layer it is enabling -- the pair is seeded only
	// while the columns are on -- and one reader for both switches keeps them in step.)
	this->columnsEnabled = config->get_bool(QStringLiteral("mascots"));
	this->backgroundEnabled = config->get_bool(QStringLiteral("mascots_background"));
	// M5 Task 4: the four name-valued keys below parse with an `ok` flag now, exactly as their
	// column equivalents do, so a value none of the accepted names matches is reported instead of
	// being replaced by the parser's fallback silently. The fallbacks themselves are unchanged.
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
	// M5 Task 4: the same one-per-process latches the columns have, for the same reason -- an
	// unsupported value is a statement about the configuration, not an event. An empty value is not
	// reported: that is the key simply not being written, which `Config` answers with the default.
	// The ungated `qWarning()` beside each debug line is the one that survives a release run, where
	// a silently different layer is exactly what the user needs to see the cause of.
	if (!this->warnedUnsupportedBackgroundMode && !backgroundModeValue.isEmpty() && !backgroundModeOk) {
		this->warnedUnsupportedBackgroundMode = true;
		this->logBackground(QStringLiteral("mascots_background_mode=\"%1\" is not one of Static, Scroll or Stripes; using Static")
			.arg(backgroundModeValue));
		qWarning() << "mascots_background_mode:" << backgroundModeValue
			<< "is not one of Static, Scroll or Stripes; using Static.";
	}
	if (!this->warnedUnsupportedBackgroundFill && !backgroundFillValue.isEmpty() && !backgroundFillOk) {
		this->warnedUnsupportedBackgroundFill = true;
		this->logBackground(QStringLiteral("mascots_background_fill=\"%1\" is not one of Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit")
			.arg(backgroundFillValue));
		qWarning() << "mascots_background_fill:" << backgroundFillValue
			<< "is not one of Crop, Fit, Stretch, HeightFit or WidthFit; using HeightFit.";
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
	// (M4 review fix: the fallback above is the shipped default. It was 0.5 -- the value this key
	// had when Task 2 read it -- but Task 3's correction made the rows the scrim and the default
	// 1.0, so a config written before the key existed would otherwise come up at half strength
	// while a fresh one comes up at full.)
	// (M5 Task 8: the key is not this controller's any more. `mascots_background_opacity` is the
	// alpha of the veil MainWindow paints over the layer, and the layer itself runs at full strength
	// -- fading it here as well would dim it twice. Nothing in this class reads or writes that key.)


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
	// M3 Task 5: the budget is spent across the bands the layout has just produced, and the answer
	// has to exist before ensureVideoSlots() below creates or drops any player. It runs after
	// applyMode() because that is what has settled each panel's band count, and before the loop
	// that pushes the live player settings because that loop now reaches every band's players.
	this->applyBandBudget();
	// M4 Task 2: columns first, background last -- what is left of the decoder budget after the
	// pair's allocation above is the background's, and this is the one place it is spent. It runs
	// after applyBandBudget() so the pair's entitlement exists to subtract, and before
	// ensureVideoSlots() below so every player the layer builds is one the budget funded.
	this->applyBackgroundBudget();
	// The layer follows the two switches from here: a settings Apply turns it on or off live,
	// without a restart. The budget above has settled how many players it may spend, so the bands
	// the surface is given are the funded prefix bandCount() derives from them.
	this->syncBackgroundSurface();
	if (this->backgroundState.surface) {
		// The bands follow the allocation: a shrink retires them from the tail through syncBands(),
		// which is the background's answer to losing players (it never takes an image in their
		// place).
		this->syncBands(this->backgroundState, MascotPanel::Background);
		// (M4 review fix: the funded prefix is what decides whether the layer has anything to draw,
		// so this is where the surface becomes visible or goes quiet again -- an empty layer is
		// hidden rather than left as a black sheet behind the rows.)
		this->updateBackgroundVisibility();
		// `mascots_background_opacity` is presentation, not a gate: zero is applied like any other
		// value, because the band logic -- picks, cadence, the player budget -- must not depend on
		// how visible the layer currently is.
		// (M5 Task 8: it is the veil's alpha now and MainWindow applies it, so zero is still not a
		// gate -- it is simply not this class's value to apply.)
		// M4 Task 6: the palette may have been replaced since the layer was built (a theme Apply
		// reaches this same method), so the backdrop follows it here as well as at creation.
		this->refreshBackgroundBackdrop();
	}

	// M5 Task 8: the veil's two keys, from the pair's side. Called here and at creation, because the
	// pair can be built after this apply and would otherwise keep the QML defaults.
	this->refreshColumnsVeil();

	const bool mirror = this->mirrorEnabled();
	// M3 Task 2: the player half below stays on `band(0)` for now. The extra bands a `Stripes`
	// panel has are image-only until Task 5 gives each one a player inside the decoder budget, so
	// there is no second band whose players these live settings could have to reach -- which is
	// why the loop reads the panel's policy band and not every band.
	// (M3 Task 5: a `Stripes` panel's extra bands inside the budget own players now, so the three
	// live settings are pushed to every band's players. The panel-level writes above them -- the
	// surface mirror and the flip parity -- stay once per panel, because they are the panel's, not
	// a band's. `Static` is one band and makes exactly the calls it made before.)
	for (PanelState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		// The surface-wide default. The per-slot value written by applyDisplay() below is
		// what is drawn, so the default can never disagree with the picture.
		state->surface->setMirror(mirror);
		// Applying config drops a user flip, which is what the retired renderer did when the
		// settings were applied: the side rule becomes authoritative and the repaint below draws
		// what the settings say, never a leftover toggle.
		state->flipToggled = false;
		for (auto& owned : state->bands) {
			MascotBand* band = owned.get();
			if (!band || !band->video())
				continue;
			// The clip rules are live settings as much as the mirror is, and createVideoSlot() must not
			// be their only reader: `mascots_clip_seconds` also re-arms the clip timer below, so a slot
			// still holding the previous length picks its start offset from a range that length no
			// longer means -- 30 s down to 5 s leaves a 30 s range, and a clip can then open 5 s before
			// the end and visibly restart mid-clip. `mascots_random_start` off fails the other way: a
			// slot created under `True` keeps randomising until it is rebuilt. Pushed here, so one
			// Apply moves the timer and the offset range together instead of letting two consumers of
			// one key disagree.
			band->video()->setRandomStart(this->randomStart, this->clipSeconds);
			// The same "one key, one reader" rule for the waits: a slot created earlier has to hear the
			// new deadline and the new retry budget, or the two consumers of `mascots_swap_timeout_ms`
			// (this class's retry timers and the slot's own deadline) would disagree until the panel was
			// rebuilt. Pushed here rather than only in createVideoSlot() for exactly the reason
			// setRandomStart() is.
			band->video()->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
			// The preload switch is a live setting too, and turning it *off* has to be more than a guard
			// on the next preload: a clip parked before the Apply would still be there, hold its paused
			// decoder, and be served by the next swap as `preloaded=yes` -- i.e. the switch would only
			// take effect one clip later, and the panel would show behaviour the user had just turned
			// off. Discarded here so "off" means off from the next swap, exactly as it does from
			// startup. An armed preload goes with it: discardPreload() drops both states.
			if (!this->preloadEnabled)
				band->video()->discardPreload();
		}
	}
	// M4 Task 2: the background's players take the same three live settings, but the two
	// panel-level writes above them are the pair's: the background never mirrors (mirrorParityFor()
	// is false for it) and has no flip parity to reset. Its own loop keeps that separation, so the
	// pair's block above reads exactly as it did.
	if (this->backgroundState.surface) {
		for (auto& owned : this->backgroundState.bands) {
			MascotBand* band = owned.get();
			if (!band || !band->video())
				continue;
			band->video()->setRandomStart(this->randomStart, this->clipSeconds);
			band->video()->setTimings(this->swapTimeoutMs, this->retryIntervalMs, this->retryAttempts);
			if (!this->preloadEnabled)
				band->video()->discardPreload();
		}
	}
	// A mode change repaints what is already on screen; the surface default alone would only
	// affect a slot created later.
	this->applyDisplay(this->leftState, MascotPanel::Left);
	this->applyDisplay(this->rightState, MascotPanel::Right);
	// M4 Task 2: the background's fill mode is `mascots_background_fill` in its `Static` shape, so
	// a settings Apply repaints the clip already on screen with the new one, exactly as the pair's
	// two calls just did for theirs.
	this->applyDisplay(this->backgroundState, MascotPanel::Background);
	this->updateLayout();
	// The player half follows the new media setting and the new budget: an `Images` setting, or a
	// budget below what a panel needs, gives the panel back its memory instead of leaving idle
	// players behind, and a setting that becomes video-capable builds them.
	const bool downgraded = this->ensureVideoSlots();
	// A config apply can change `mascots_pause_unfocused` or the clip length under a running
	// panel, so the new rules are applied to what is playing now.
	this->applyVideoPaused();
	// M3 Task 4: the scroll driver follows the same rules and one more -- it exists only while a
	// mode with a pan in it is configured and the panels can be seen. Reconciled after applyMode()
	// above, so a change of mode starts or stops it in the same turn the layout changed, and after
	// applyDisplay() so a panel leaving `Scroll` has already had its offset reset by applyMode().
	this->updateScrollDriver();
	// The clip timer is re-derived as well, because the mirror key and the clip length are both
	// inputs to it: the right panel's timer is stopped while the pair is mirrored, so turning the
	// key off mid-run would otherwise leave that panel on its clip until its next commit. The cost
	// is that an Apply pushes the next swap out by a whole clip, which is what the new length asks
	// for anyway.
	// (M3 Task 6A: every band that is showing a video re-derives its own timer -- a `Stripes` panel
	// has one cadence per band now, and an Apply that reached band 0 alone would leave three
	// stripes on the old length. `Static` is one band and restarts exactly the one timer it did.)
	// (M4 Task 2: the background's bands are in this list now. The clip timer is a per-panel
	// cadence, not pair policy, so a clip length or mirror change reaches the layer's own timers the
	// same way it reaches the columns'.)
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
	// A panel that lost its players is drawing nothing, and nothing else will fill it: the beat
	// skips a panel by what it shows (which is now nothing) and the clip timer belonged to players
	// that no longer exist. So the apply owes the pair one pick -- queued, because this runs inside
	// a settings handler and a pick walks the mascot directory and posts a decode. One pick for the
	// pair rather than one per panel, so the mirror key keeps deciding whether the two panels show
	// the same file.
	if (downgraded && this->running) {
		// M4 Task 2: the pair is only repainted when it is on. With the columns off there is nothing
		// showing the media a downgrade would replace, and the pick would decode for hidden hosts.
		if (this->columnsEnabled) {
			this->logVideo(QStringLiteral("panels downgraded from video to images; repainting them"));
			QMetaObject::invokeMethod(this, [this]() { this->nextImages(false); }, Qt::QueuedConnection);
		}
	}
	// M3 Task 6A: a `mascots_media` change while a `Stripes` panel is up is the one thing that can
	// leave its bands on the wrong kind of content for the session: the bands past the first are
	// filled once and have no beat of their own in this task, so nothing would re-ask them. Queued
	// for the reason the downgrade pick above is: this runs inside a settings handler and a pick
	// walks the mascot directory. A single-band panel is deliberately left alone -- its own next
	// swap (the beat, or the clip timer) already follows the new setting, and picking it here would
	// add a swap the shipped path never makes.
	// (M3 Task 6A fix: the separate entry point this paragraph introduced is gone -- the paragraph
	// is kept as the record of why one was needed. The re-fill now happens where the transition
	// actually is: ensureVideoSlots() queues one refill per band it *gives* a player to while the
	// band is not showing a video, which is exactly an Images -> Videos apply, and the other
	// direction keeps the downgrade path above. One content action per band instead of three
	// competing ones on the same Apply.)
	// M4 Task 2: a background that has just been turned on (or given more bands by a budget
	// increase) has bands with nothing in them, and the pair's fill above deliberately refuses it
	// -- an image is the one thing the layer must never show. One pick per empty band, through its
	// own video-only path, queued for the same reason the two picks above are.
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

StripesOrientation MascotController::stripesOrientationFromString(const QString& value, bool* ok)
{
	// M3 Task 5: the same shape as modeFromString(), for the same reason -- one place turns the
	// configured text into the value every layout path reads, so no path parses a string and a
	// value this class cannot read cannot reach the geometry. The fallback is `Horizontal`, the
	// documented default and the axis every Stripe panel stacked along before the key existed.
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
	if (ok)
		*ok = false;
	// The mode M1 shipped, and what applyConfig() falls back to: a caller that ignores `ok` still
	// gets the height-normalised rendering every existing panel has, which is also the only
	// fallback that cannot change a picture just because a key was misspelled (Ruling M3-2).
	return MascotSurface::FillMode::HeightFit;
}

ScrollDirection MascotController::scrollDirectionFromString(const QString& value, bool* ok)
{
	// M3 Task 4: same shape as the two parsers above, for the same reason -- one place turns the
	// configured text into the value the driver reads, so no tick ever parses a string. The
	// fallback is `LeftToRight`, the documented default *and* the sense a horizontal pan takes
	// from a vertical direction (Ruling M3-4), so an unreadable value still pans rather than
	// stopping the band.
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
	// (M3 Task 5: the last sentence is superseded. A vertical `Stripes` panel is columns, its bands
	// are normalised to their width and the *height* is what overflows, so it answers `WidthFit`.
	// `Scroll` keeps `HeightFit`: §7.6 defines it as content sized wider than the panel, and Ruling
	// M3-4 keeps it on the horizontal axis, so it has no vertical variant to follow.)
	const PanelLayout& layout = this->layoutFor(panel);
	// (M4 Task 6: a background `Scroll` cell no longer pans its content -- the marquee moves the
	// cell itself -- so it has no axis to normalise and honours `mascots_background_fill` exactly
	// as `Static` does. The pair's `Scroll` still forces `HeightFit` below: its one band is still
	// wider than the panel and its content is what travels.)
	if (panel == MascotPanel::Background && layout.mode == MascotMode::Scroll)
		return layout.fill;
	if (layout.mode == MascotMode::Static)
		return layout.fill;
	if (layout.mode == MascotMode::Stripes
		&& layout.orientation == StripesOrientation::Vertical)
		return MascotSurface::FillMode::WidthFit;
	return MascotSurface::FillMode::HeightFit;
}

int MascotController::bandCount(MascotPanel panel) const
{
	// `Static` and `Scroll` are one band by definition -- Scroll's band *is* the panel, and only
	// the content inside it moves (Task 4) -- so this task's whole layout asks the same question
	// of both, and a one-band panel is exactly the panel that existed before a mode did.
	// M4 Task 2: the background's count comes from its players instead of from its mode's counts,
	// because a band with no player behind it would be a band drawing nothing: 0 with no player, 1
	// for `Static`/`Scroll` when it has one, and the funded prefix for `Stripes`. That is the clamp
	// a shrinking budget uses -- syncBands() retires the bands this stops counting.
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount();
	if (this->layoutFor(panel).mode != MascotMode::Stripes)
		return 1;
	return this->stripesCountFor(panel);
}

int MascotController::backgroundBandCount() const
{
	// (M4 Task 6: `Scroll` is the seamless marquee now, so it answers the same funded prefix
	// `Stripes` does -- the bands the budget actually bought out of
	// `mascots_background_stripes` -- instead of the single full-window band the old pan drew
	// inside. `Static` keeps its one band, and its two-player crossfade with it.)
	if (this->backgroundPlayers <= 0)
		return 0;
	if (this->backgroundLayout.mode == MascotMode::Stripes
		|| this->backgroundLayout.mode == MascotMode::Scroll)
		return std::min(this->backgroundStripes, this->backgroundPlayers);
	return 1;
}

bool MascotController::backgroundMarqueeActive(MascotPanel panel) const
{
	// M4 Task 6: the marquee needs a divisor (`count - 1`), so one funded band is answered as a
	// static full-window cell by bandRects() and never reaches the translating path. `Static` and
	// `Stripes` keep their own shapes, so this is the only mode whose layout the driver rewrites.
	return panel == MascotPanel::Background
		&& this->layoutFor(panel).mode == MascotMode::Scroll
		&& this->bandCount(panel) >= 2;
}

bool MascotController::bandMirrorsLeft(int bandIndex) const
{
	// M3 Task 6A: the pair rule extended from one band to N. Band i of the right panel mirrors
	// band i of the left panel exactly while the left panel has a band i to mirror -- the two
	// configured counts do not have to agree, and a band with no counterpart keeps its own picks
	// and its own clip timer rather than waiting for a commit that will never come.
	return this->mirrorEnabled() && bandIndex >= 0 && bandIndex < this->bandCount(MascotPanel::Left);
}

QVector<QRectF> MascotController::bandRects(MascotPanel panel) const
{
	const PanelState& state = this->stateFor(panel);
	if (!state.surface)
		return QVector<QRectF>();
	// The mode's stacking axis. Every mode this task reads stacks its bands top to bottom (the
	// `Stripes` default orientation, because Task 5 owns the key that would choose otherwise), so
	// a band's rectangle is a horizontal slice of the panel. Slicing the surface's own rectangle
	// is what keeps every band inside the panel and their sum equal to the panel itself.
	// (M3 Task 5: the key is read and the axis comes from stripesStackVertically(). A `Vertical`
	// `Stripes` panel is sliced into columns instead, and every single-band mode still asks for the
	// top-to-bottom answer -- which `slicedBandRects()` answers with the panel itself.)
	// (M4 Task 6: the background's `Scroll` is the one case that is not a slice: its cells are the
	// marquee rects, translated by the panel's own pixel offset. The pair's `Scroll` still slices
	// its one full-panel band and pans the content inside it; `Stripes` on either kind of panel
	// still slices exactly as before.)
	const QRectF frame(state.surface->rect());
	if (this->backgroundMarqueeActive(panel)) {
		const int count = this->bandCount(panel);
		const bool vertical = this->scrollPanIsVertical(panel);
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
	// M3 Task 5: the stacking axis, not the panning axis. `Horizontal` stripes are strips stacked
	// top to bottom, which is `slicedBandRects()`'s `stackVertically` true; `Vertical` stripes are
	// columns stacked left to right. A single-band mode (`Static`, `Scroll`) is the panel itself
	// either way, so the top-to-bottom answer is also the shipped default there.
	const PanelLayout& layout = this->layoutFor(panel);
	return layout.mode != MascotMode::Stripes
		|| layout.orientation == StripesOrientation::Horizontal;
}

void MascotController::applyMode()
{
	// M3 Task 6A: never from inside a wrap-point commit. Such a commit runs between the scroll
	// driver's band loop and the next event-loop turn (onScrollTick() runs the collected commits
	// after the loop, so no band or surface pointer is held across it), but a content commit has
	// no business rebuilding the band vector at all. Re-queued instead of applied, so a change
	// that arrives that way still happens -- on the next turn, with no tick iterating anything.
	if (this->wrapCommitDepth > 0) {
		QMetaObject::invokeMethod(this, [this]() { this->applyMode(); }, Qt::QueuedConnection);
		return;
	}
	// Nothing to lay out yet: applyConfig() runs before createSurfaces() on the enable path, so
	// the first Apply has no panel to act on. configureSurface() applies the same layout when the
	// surface does appear and start() fills the bands it has; nothing is recorded here, which is
	// what makes the next Apply with surfaces run this again.
	// (M4 Task 2: the background counts as a panel to act on -- a controller with the columns off
	// but the layer on still has a layout to settle.)
	if (!this->leftState.surface && !this->rightState.surface && !this->backgroundState.surface)
		return;
	// Idempotent by construction: an Apply that changed neither the mode nor either panel's live
	// band count has nothing to re-derive, and re-running the fill would walk the mascot directory
	// for bands that already have content.
	const bool modeChanged = this->columnsLayout.mode != this->columnsLayout.appliedMode;
	bool countChanged = false;
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		const PanelState& state = this->stateFor(panelOf(side));
		if (state.surface && state.configuredBandCount != this->bandCount(panelOf(side)))
			countChanged = true;
	}
	// M3 Task 5: an orientation change alters the stacking axis without changing the band count,
	// so it has to be part of the relayout condition or a `Horizontal` -> `Vertical` switch would
	// leave the old rectangles in place. It is only a `Stripes` question: no other mode derives a
	// rectangle from the orientation, and asking it while `Static`/`Scroll` is up would make an
	// orientation key nothing draws from re-lay the panels out.
	const bool orientationChanged = this->columnsLayout.applied
		&& this->columnsLayout.mode == MascotMode::Stripes
		&& this->columnsLayout.orientation != this->columnsLayout.appliedOrientation;
	// M4 Task 2: the background's layout is compared on its own latches, because it is not the
	// pair's layout and its band count comes from its players rather than from its mode. Only a
	// surface that exists is asked: a layer that is off has no bands to lay out, and its latches
	// are written when it is built.
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
	// M4 Task 2: the background's bands are reconciled here too -- this is the one layout entry,
	// so its count is brought in line with bandCount(Background) on the same turn the pair's is.
	if (this->backgroundState.surface)
		this->syncBands(this->backgroundState, MascotPanel::Background);
	this->columnsLayout.appliedMode = this->columnsLayout.mode;
	this->columnsLayout.appliedOrientation = this->columnsLayout.orientation;
	this->columnsLayout.applied = true;
	// M4 Task 2: the background's latches describe the layout syncBands() has just applied to it.
	this->backgroundLayout.appliedMode = this->backgroundLayout.mode;
	this->backgroundLayout.appliedOrientation = this->backgroundLayout.orientation;
	this->backgroundLayout.applied = true;
	// M3 Task 4: a mode change starts a fresh pass -- the phase is a fraction of a travel whose
	// meaning has just changed -- and re-arms the per-band zero-travel line, which is a statement
	// about the layout and not about the session. A count change alone leaves both alone: the
	// surviving bands keep their position, so a `Stripes` count change reads as a relayout rather
	// than as a jump.
	if (modeChanged) {
		for (PanelState* state : { &this->leftState, &this->rightState }) {
			state->scrollPhase = 0.0;
			state->scrollZeroTravelLogged.fill(false);
		}
		// And a mode with no pan in it has no offset: `Static` must draw the picture the panel drew
		// before modes existed, so the last pan position is taken off the slots here rather than
		// left for the surface to replay (applySlot() restores every mirrored value, the scroll
		// offset among them).
		// (M3 Task 5: the call is unconditional now. A mode change resets the phase above and the
		// offsets below, so a new layout starts from the near end instead of inheriting a position
		// measured on the previous one -- and a band entering a vertically panning layout cannot
		// keep an offset that was written on the horizontal axis, which no tick would clear while
		// the speed is 0. `Static` is the case the original sentence describes and is unchanged.)
		// (M4 Task 4: the pair's two panels, named one at a time. resetScrollOffsets() resets one
		// panel now, so a columns mode change cannot take the layer's offsets or its phase with it
		// -- the background resets its own pass below, when its own mode is the one that moved.)
		this->resetScrollOffsets(MascotPanel::Left);
		this->resetScrollOffsets(MascotPanel::Right);
	}
	// M4 Task 2: the background's mode is its own, so it resets its own pass when *its* mode
	// changes -- the pair's phase above is not touched by a background relayout, and the reset
	// below reaches the layer's bands as well because resetScrollOffsets() is a per-panel walk.
	// (M4 Task 4: the reset below names the layer instead of walking every panel, which is what
	// makes the sentence above true in the other direction too -- a background relayout no longer
	// clears the columns' offsets or their phase.)
	if (backgroundModeChanged) {
		this->backgroundState.scrollPhase = 0.0;
		this->backgroundState.scrollZeroTravelLogged.fill(false);
		this->resetScrollOffsets(MascotPanel::Background);
	}
	// M3 Task 5: an orientation change keeps the phase -- the fraction is axis-agnostic, so the
	// bands keep their place in the pass instead of snapping to the near end (Review Focus 8's
	// rule, applied to the other quantity that changes a travel) -- but the offsets were written
	// on the old axis. Clearing them is what stops a band drawing a horizontal pan while it is
	// stacked vertically; without it a non-zero offset would survive whenever no tick writes the
	// other axis, and a speed of 0 would leave it there for good. The zero-travel latch re-arms
	// because the layout it described has just moved under it.
	if (orientationChanged) {
		for (PanelState* state : { &this->leftState, &this->rightState }) {
			MascotSurface* surface = state->surface.data();
			if (surface) {
				for (auto& owned : state->bands) {
					MascotBand* band = owned.get();
					if (!band || band->scrollOffset() == 0.0)
						continue;
					// The one-write path, not an applySlot(): the slot's other values are untouched
					// and the surface's mirror keeps 0, so a recreated delegate comes back centred.
					surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
					band->setScrollOffset(0.0);
				}
			}
			state->scrollZeroTravelLogged.fill(false);
		}
	}
	// M4 Task 2: the same rule for the background's own stacking axis. The background has a
	// separate layout, so its orientation change is its own: the offsets it wrote on the old axis
	// go, and the zero-travel latch re-arms because the layout it described has moved.
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
	// (M3 Task 5: added, as `orientationChanged`. The condition is exactly "the rectangles may have
	// moved": a count change or an axis change, never the mode alone.)
	// (M3 Task 5 fix: the axis change is *not* added here after all. applyConfig() calls
	// updateLayout() unconditionally after applyDisplay(), and the orientation is cached before
	// applyMode() runs, so that call already produces the new rectangles; adding this one wrote
	// every band's rect twice on an orientation change -- the duplicate c4d9fd5 removed. The count
	// path stays: syncBands() has just changed the slot count, and the layout must be settled
	// before applyDisplay() below repaints into it. `orientationChanged` still makes applyMode()
	// run, for the offset and zero-travel-latch clearing above, which no later call performs.)
	// M4 Task 2: a background count change moves the layer's rectangles too, so it asks for the
	// same relayout the pair's count change does.
	if (countChanged || backgroundCountChanged)
		this->updateLayout();
	// Bands the change added have no content. Queued, because this runs inside a settings handler
	// and a pick walks the mascot directory and posts a decode; a controller that was stopped in
	// between simply never runs it, and fillEmptyBands() asks the same guards again.
	QMetaObject::invokeMethod(this, [this]() { this->fillEmptyBands(); }, Qt::QueuedConnection);
}

void MascotController::syncBands(PanelState& state, MascotPanel panel)
{
	MascotSurface* surface = state.surface.data();
	if (!surface)
		return;
	const int wanted = this->bandCount(panel);
	// Shrink from the tail, before the slot count shrinks. A band's players draw into QVideoSinks
	// that belong to its slot's QML delegate, and MascotVideoSlot's contract is that those sinks
	// outlive its shutdown() -- so the bands that are going must be destroyed while their delegate
	// still exists. This is the same order releaseSurface() keeps for a whole surface, applied to
	// the bands a count change removes.
	QStringList removedShown;
	while (static_cast<int>(state.bands.size()) > wanted) {
		removedShown.push_back(state.bands.back()->media().path);
		// (M3 Task 6A: the band's speculative candidate goes with it. It is a pinned decode, so a
		// destroyed band that held one would otherwise leave its path pinned for the session --
		// the shrink path's own version of the owner-field invariant below.)
		removedShown.push_back(state.bands.back()->playback().candidate.path);
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
	// M3 Task 4: the per-band zero-travel latch travels with the bands. A surviving band keeps
	// whether its line was already written -- nothing about its content changed -- and an index
	// that has just appeared starts un-written, so a band added by a count change says its piece
	// once.
	state.scrollZeroTravelLogged.resize(wanted);
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
	// M4 Task 2: the pair's fill has nothing to do while the columns are off. The background keeps
	// the controller running in that state, so a layout Apply would otherwise walk the mascot
	// directory and decode a picture for two hidden panels that never draw it.
	if (!this->columnsEnabled)
		return;
	// M4 Task 2: this fill refuses the background, and the refusal is structural: it is a fill with
	// an *image*, and the background shows video only (a portrait still stretched across the window
	// is exactly what the layer must never draw). The loop below is therefore the pair's and stays
	// the pair's -- appending Background to it would hand the layer a still. The background's answer
	// to a band with no player is the band-count clamp in bandCount(), applied by syncBands().
	// M3 Task 6A: the pair fills one band index at a time, and the left panel is the one that
	// chooses it. Under the mirror key a right-panel band the left panel has is filled from the
	// left band's pick and commit (nextBandImages() for the policy band, showMedia()'s image copy
	// and the video first-frame copy for the rest), so drawing for it here would give it a file of
	// its own and start a second load. A right-panel band past the left panel's count has no
	// counterpart to mirror, so it is filled like any other band.
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
				// M3 Task 6A: a right-panel band the left panel has is filled from the left band's
				// pick and commit (showMedia()'s image copy, the video first-frame copy), not by a
				// draw of its own. A right-panel band past the left panel's count has no counterpart
				// to mirror, so it is filled like any other band and never reaches this branch.
				// (M3 Task 6A fix: a band that appears *after* its left counterpart already has
				// content -- a live `mascots_stripes_right` increase under the mirror key -- used to
				// wait for the left band's next commit. That is a whole clip away, and for a still or
				// with `mascots_clip_seconds = 0` it is never. The left band's committed media is
				// copied here instead, so a stripe increase is visible immediately: an image is
				// committed through the same mirror call showMedia() uses, a video is started at the
				// same offset through startMirrorVideo(). Only a band whose left counterpart is
				// empty, or still loading, waits -- and a load arms the pending copy so its first
				// frame does the copy.)
				MascotBand* leftBand = this->leftState.band(i);
				const bool mayOwn = this->bandMayOwnPlayer(panelOf(side), i);
				if (mayOwn && leftBand
					&& (leftBand->requestIsVideo() || leftBand->playback().videoPickRetryPending)) {
					// Still loading: the first frame is where the pair's shared offset exists.
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
				// (M3 whole-branch fix: the left band's in-flight request may be an *image* -- it
				// always is when both bands of a pair have just lost their players (a live budget
				// 4->2 on 2+2, or 8->4 on 4+4), because the pair's image pick is what follows the
				// degradation. That image's delivery is exactly what copies onto this band, so
				// picking here would post a second decode for one picture and show the pair
				// disagreeing until it landed. Wait for the copy instead; only a video this band
				// cannot show falls through to its own still.)
				if (leftBand && !leftBand->requestPath().isEmpty() && !leftBand->requestIsVideo())
					continue;
				// Nothing this band can take from the left *yet*. A band that may own a player -- or
				// a panel under an images-only setting, where the left band's pick is always an
				// image -- waits for the left's next commit, which copies. A player-less band in a
				// video setting cannot receive the video the left band may commit, so it falls
				// through to this loop's own still instead of waiting for a copy that may never come
				// (Ruling M3-8: a decoder-short band shows an image).
				if (mayOwn || !this->mediaSettingWantsVideo())
					continue;
			}
			// A request in flight *is* this band's content: asking again would post a second
			// decode for one band, and for band 0 would walk the mascot directory twice for one
			// fill. `isPending` is asked rather than the path alone for the reason pickFor() asks
			// it -- a request that produced nothing at all must not wedge the band.
			// (M3 Task 6A: a video load is in flight exactly while requestIsVideo() is set, and the
			// prefetcher only knows about image decodes, so `isPending` would always answer "no"
			// for a clip that is opening and the band would be picked a second time over its own
			// load. The flag is asked first for that reason.)
			if (!band->requestPath().isEmpty()) {
				if (band->requestIsVideo())
					continue;
				MascotPrefetcher* source = this->prefetcher.data();
				if (!source || source->isPending(band->requestPath()))
					continue;
			}
			if (i == 0) {
				// The panel's policy band takes the panel's own pick -- the call the seeding swap,
				// the beat and the clip timer all make -- so a single-band panel (`Static`,
				// `Scroll`) fills exactly as it did before bands were counted, candidate path and
				// all. That is the loop's one iteration in `Static`.
				// (M3 Task 6A: the same call, naming the band by its index now; every band below
				// takes that same path, so this is the first iteration of one rule rather than the
				// only one.)
				// (M3 Task 6A fix: under the mirror key the policy band's fill is the *pair's*
				// fill, through nextBandImages(), because band 0's image commit does not itself copy
				// an image onto the right panel -- only the pair path copies the request. A right
				// panel's band 0 never reaches this block: the mirror branch above handles it.)
				if (mirror)
					this->nextBandImages(0, false);
				else
					this->pickFor(state, panelOf(side), i, true);
				continue;
			}
			// A band past the first has no player in this task: the per-band decoder budget is
			// Task 5's, and the cadence that would advance an extra band is Task 6's. That makes
			// it deliberately an *image* band -- filled once here through the same source and the
			// same request/delivery path an image pick uses, and then left showing that picture
			// until the next layout change. The degradation has a known end, and the alternative
			// this task could have offered is a band with no media at all, which is worse than a
			// still.
			// (M3 Task 5: a band past the first inside the decoder budget may own a player now, but
			// this fill is still an image on purpose: the pick that would load a video into it is
			// Task 6's per-band cadence. The budget decides which of these bands may later show
			// video; until then they draw what this path gives them, which is the "band never
			// blank" floor.)
			//
			// Drawing the path here rather than calling pickFor() is deliberate: pickFor() owns
			// the panel's single candidate slot, its speculation and its video sequence, all of
			// which belong to band 0 until Task 5/6 give the bands their own. A pick that consumed
			// the panel's candidate for a picture on band 1 would cost band 0 its predecoded swap,
			// and one that started a video load would start it with no player to receive it.
			// (M3 Task 6A: no longer true, which is exactly what this task changed. The state that
			// made a shared pick unsafe -- the candidate, its prefetched path and the video sequence
			// -- is per band now (MascotBand::Playback), and the band's own player is the one
			// pickFor()'s video branch loads into, so pickFor() is the right call here and a fill
			// can no longer steal band 0's predecoded swap.)
			this->pickFor(state, panelOf(side), i, false);
		}
	}
}

void MascotController::updateLayout()
{
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		if (!state.surface)
			continue;
		// The slot fills the surface that hosts it, and the surface derives the content shift
		// from this rectangle itself (amendment R: no centring arithmetic here).
		// M3 Task 1: the rectangle goes through the band, which is the only thing that writes
		// into a slot now.
		// M3 Task 2: one rectangle per band, from the mode's slices of this surface. A single-band
		// panel gets the panel's own rectangle back unchanged (slicedBandRects() returns the panel
		// itself for a count of one), so `Static` writes exactly the rectangle it always wrote.
		const QVector<QRectF> rects = this->bandRects(panelOf(side));
		for (int i = 0; i < static_cast<int>(state.bands.size()) && i < rects.size(); ++i)
			state.bands[static_cast<std::size_t>(i)]->setRect(rects.at(i));
	}
	// M4 Task 2: the background layer is the window's rectangle, not a slice of a column, so its
	// geometry is written here from its host -- it is in no layout, which is exactly why nothing
	// else can size it. This is reached from every window resize (MainWindow::event() ->
	// applyMascotWidth() -> updateLayout()), so there is deliberately no second resize hook. A
	// host that has not been laid out yet (a zero rectangle) is skipped: positioning the layer at
	// a degenerate size would only be corrected by the next event, and the bands below would be
	// sliced from geometry nobody asked for.
	if (this->backgroundState.surface) {
		QWidget* host = this->backgroundHost.data();
		if (host && host->width() > 0 && host->height() > 0)
			this->backgroundState.surface->setGeometry(host->rect());
		// One band in this task's `Static` shape, N in a later mode's -- the same slices the pair
		// above receives, from the background's own layout and the surface's own rectangle.
		const QVector<QRectF> rects = this->bandRects(MascotPanel::Background);
		for (int i = 0; i < static_cast<int>(this->backgroundState.bands.size()) && i < rects.size(); ++i)
			this->backgroundState.bands[static_cast<std::size_t>(i)]->setRect(rects.at(i));
	}
	// M2: the panel's window is what the pause rule watches, and this is the place a surface
	// learns which window it ended up in (it is parented to a host inside the main window). Only
	// the first call for a given window does anything: watchWindow() returns early otherwise.
	QWidget* window = nullptr;
	if (this->leftState.surface)
		window = this->leftState.surface->window();
	if (!window && this->rightState.surface)
		window = this->rightState.surface->window();
	// M4 Task 2: with the columns off, the background is the only surface that can supply the
	// window the pause rule watches; without this a background-only run would never learn it.
	if (!window && this->backgroundState.surface)
		window = this->backgroundState.surface->window();
	this->watchWindow(window);
}

double MascotController::scrollTravel(MascotPanel panel, const MascotBand& band) const
{
	// The one geometry source (Task 3's seam): slotContentSize() answers the size the media is
	// actually painted at -- an invalid slot gives (0,0) and a slot with no media gives its own
	// size, so neither invents a pan -- and the band's rectangle is the window onto it. Travel is
	// the overflow the pan can spend on the mode's axis; zero means the content fits its band, so
	// the band stands still (Ruling M3-6) and the tick writes nothing for it.
	MascotSurface* surface = band.surface();
	if (!surface)
		return 0.0;
	const bool vertical = this->scrollPanIsVertical(panel);
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
		// Built on the first start rather than in the constructor: a `Static` config -- the default,
		// and the regression line every task of this milestone is measured against -- creates no
		// timer at all, and a scroll-capable mode asks for one here, once.
		this->scrollTimer = new QTimer(this);
		// Precise, because the coarse timer would round a 16 ms interval into a bucket that makes a
		// 40 px/s pan visibly uneven; the interval is the plan's ~16 ms (60 fps, §7.10's budget).
		this->scrollTimer->setTimerType(Qt::PreciseTimer);
		this->scrollTimer->setInterval(SCROLL_TICK_MS);
		connect(this->scrollTimer, &QTimer::timeout, this, &MascotController::onScrollTick);
	}
	if (this->scrollTimer->isActive())
		return;
	// The clock arms with the driver, never before it: a delta measured across a pause would be
	// spent as a jump on the first tick after the resume, which is exactly the stall behaviour the
	// wrap is there to bound, spent on time the panel was not even on screen.
	this->scrollClock.start();
	this->scrollTimer->start();
}

void MascotController::stopScrollDriver()
{
	if (!this->scrollTimer || !this->scrollTimer->isActive())
		return;
	this->scrollTimer->stop();
	// Invalid, not merely stopped: every start re-arms the clock, and a stale elapsed time is the
	// one thing a stop exists to get rid of.
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
	// `Static` is one band covering the whole panel: there is nowhere for it to pan, and it is the
	// mode every task's regression gate draws. `Scroll` and `Stripes` both pan -- Task 5 owns a
	// stripe's stagger and its alternate direction, not its motion -- so one comparison covers
	// both.
	// (M4 Task 2: the comparison is per panel now, because the background has its own layout: a
	// `Static` column layout no longer means nothing can pan, and a panning background alone has to
	// start the driver. Nothing exists to pan when there is no surface at all.)
	if (!this->panModeActive())
		return false;
	if (!this->panelsVisible())
		return false;
	// The pause rule the players already obey (§7.13), reused rather than reinvented: hidden and
	// minimized are caught by panelsVisible() whether `mascots_pause_unfocused` is on or off, and
	// the key itself decides whether a merely unfocused window stops the pan as it stops decoding.
	return !this->videoShouldPause();
}

bool MascotController::scrollCanTick() const
{
	if (!this->running)
		return false;
	// M4 Task 2: the mode gate is per panel here too (see scrollShouldRun()). The surface that is
	// asked is the first existing one that actually pans, so a `Static` column pair cannot hide a
	// panning background -- and the pair's own answer is unchanged when no background exists.
	MascotSurface* surface = nullptr;
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelState& state = this->stateFor(panel);
		MascotSurface* candidate = state.surface.data();
		if (!candidate || this->layoutFor(panel).mode == MascotMode::Static)
			continue;
		// A panel that cannot be seen has no tick of its own, but it must not decide the answer for
		// one that can: the background may be the only surface on screen while a hidden column is
		// still configured for `Scroll`.
		if (!candidate->isVisible() || !windowOnScreen(candidate->window()))
			continue;
		surface = candidate;
		break;
	}
	if (!surface)
		return false;
	// Hidden and minimized are "cannot be seen" whatever the config says, so they are tested here
	// and not left to `mascots_pause_unfocused` (videoShouldPause() answers false as soon as that
	// key is off, which is right for a player and wrong for a timer nobody can see). The covered
	// case is deliberately not asked: IsMyWindowVisible() costs more per call than a whole tick
	// does, and the event-driven reconciliation stops the driver when the window itself changes
	// state.
	return !this->videoShouldPause();
}

bool MascotController::panelsVisible() const
{
	// M4 Task 2: "the panels" is every surface this controller drives now -- see
	// anySurfaceVisible(). With no background surface this is exactly the pair's answer it was.
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
		// IsMyWindowVisible() is the honest "a user can see it": a minimized window is still visible in
		// Qt's sense and a covered one is not visible in the OS's.
		if (window && utils::IsMyWindowVisible(window))
			return true;
	}
	return false;
}

bool MascotController::panModeActive() const
{
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		MascotSurface* surface = this->stateFor(panel).surface.data();
		// A panel nobody can see has no pass to run: with the columns off, a still-configured
		// `mascots_mode=Scroll` must not keep the driver alive for a layer that draws `Static`.
		if (!surface || !surface->isVisible())
			continue;
		if (this->layoutFor(panel).mode != MascotMode::Static)
			return true;
	}
	return false;
}

bool MascotController::scrollPanIsVertical(MascotPanel panel) const
{
	// Ruling M3-4 in code: `Scroll` pans horizontally. §7.6 defines it as content sized wider than
	// the panel, and effectiveFillMode() forces the matching HeightFit for it, so the overflow this
	// driver has to spend is the width -- a vertical pan here would travel through a band that only
	// ever exposes one strip of the picture. `Stripes` stacks horizontally too until Task 5 reads
	// `mascots_stripes_orientation`; that key is the one place this answer changes, and no other
	// line in the driver needs to know which axis it got.
	// (M3 Task 5: read and cached now. A `Vertical` `Stripes` panel is columns, so each band's
	// overflow is its height and the pan runs down it; every other case stays horizontal. This is
	// the *panning* axis, perpendicular to stripesStackVertically()'s stacking axis, and
	// effectiveFillMode() must answer the matching normalised mode or the travel here is 0.)
	const PanelLayout& layout = this->layoutFor(panel);
	// (M4 Task 6: the background's marquee is one-dimensional and takes its axis from the panel's
	// own orientation key, because `Scroll` no longer inherits the pair's "horizontal by
	// definition" rule -- its cells travel along whichever axis they were configured for, and the
	// offset is the same pixel displacement either way. The pair keeps the answer above.)
	if (panel == MascotPanel::Background
		&& (layout.mode == MascotMode::Scroll || layout.mode == MascotMode::Stripes))
		return layout.orientation == StripesOrientation::Vertical;
	return layout.mode == MascotMode::Stripes
		&& layout.orientation == StripesOrientation::Vertical;
}

bool MascotController::scrollDirectionReverses(MascotPanel panel) const
{
	// The direction names the order a pass visits the content in, and the two "reverse" values are
	// its opposite (see ScrollDirection). A band whose pan axis disagrees with the direction's
	// therefore keeps its *sense* by keeping that order: TopToBottom on a horizontal band is the
	// forward pass, BottomToTop the backward one (Ruling M3-4, logged once by applyConfig()).
	const ScrollDirection direction = this->layoutFor(panel).scrollDirection;
	return direction == ScrollDirection::RightToLeft
		|| direction == ScrollDirection::BottomToTop;
}

bool MascotController::scrollDirectionIsVertical(MascotPanel panel) const
{
	const ScrollDirection direction = this->layoutFor(panel).scrollDirection;
	return direction == ScrollDirection::TopToBottom
		|| direction == ScrollDirection::BottomToTop;
}

double MascotController::scrollPhaseOffset(MascotPanel panel, const MascotBand& band) const
{
	// Task 5's seam (Ruling C1): the stagger is `index / count` of the pass, added to the panel's
	// phase in the same travel-length units, so a staggered band is a band whose pass starts
	// later. Nothing in this task staggers anything -- a single-band mode has one phase and no
	// second band to disagree with it -- and the driver reads this per band per tick either way.
	// (M3 Task 5: the stagger is `index / count` of the band's own panel, and the index is the
	// band's slot index, which is its position along the stacking axis. The lookup is how a band
	// that carries only its slot index finds the count of the panel it belongs to; a single-band
	// mode returns 0 without asking.
	// (M4 Task 1: the panel is the caller's now, so the count comes from the panel the tick (or
	// the beat) is holding rather than from a lookup over the pair; the paragraph above stays as
	// the record of why the lookup existed.)
	//
	// Under pingpong the offsets are accepted as they are, not normalised: the triangle fold maps a
	// phase x and 2-x to the same drawn position, so two staggered bands do coincide for an
	// instant as their legs cross -- with count 3, offsets 0 and 2/3 meet at phase 2/3. That is
	// inherent to a reflection: every band traverses the same segment twice per cycle, so any two
	// distinct offsets on it meet somewhere. Only the sawtooth (wrap) mode keeps them apart
	// forever, and wrap mode is what a non-pingpong panel uses. The offsets are still distinct, so
	// the bands never lock together; a momentary crossing is the woven look the stagger is for.
	// Shortening the phase to dodge the fold was rejected: it would halve the stagger and still
	// leave a collision for some count, because the fold is 2-to-1 whatever the offsets are.)
	const PanelState& state = this->stateFor(panel);
	if (state.band(band.slotIndex()) != &band)
		return 0.0;
	const int count = this->bandCount(panel);
	if (count <= 1)
		return 0.0;
	return static_cast<double>(band.slotIndex()) / static_cast<double>(count);
}

bool MascotController::scrollReversed(MascotPanel panel, const MascotBand& band) const
{
	// Task 5's other seam (Ruling C1): `mascots_stripes_alternate` reverses odd bands. It is a
	// per-band sign applied on top of the configured direction rather than folded into it, so the
	// two can be changed independently, and it is false here because nothing in this task
	// reverses a band.
	// (M3 Task 5: odd `Stripes` bands when the key is on; `Static` and `Scroll` have one band and
	// no odd index, so they answer false whatever the key says.)
	const PanelLayout& layout = this->layoutFor(panel);
	return layout.mode == MascotMode::Stripes
		&& layout.alternate
		&& (band.slotIndex() % 2) != 0;
}

void MascotController::resetScrollOffsets(MascotPanel panel)
{
	// M4 Task 2: every panel that has a surface, the background included -- the offset it writes
	// is a per-panel one, so a layout change on any of them takes the pan off that panel's slots.
	// (M4 Task 4: "every panel" is the caller's panel now. applyMode() calls this once for each
	// panel whose own mode changed, so the walk below is that panel's bands and the phase reset
	// below is that panel's phase -- the rule the M3 Task 5 note gives, without reaching across to
	// a panel whose layout did not move.)
	PanelState& state = this->stateFor(panel);
	MascotSurface* surface = state.surface.data();
	if (surface) {
		for (auto& owned : state.bands) {
			MascotBand* band = owned.get();
			if (!band || band->scrollOffset() == 0.0)
				continue;
			// The one-write path, not an applySlot(): the slot's other values are untouched, and
			// the surface's mirror keeps the 0 so a delegate QML recreates comes back centred.
			surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
			band->setScrollOffset(0.0);
		}
	}
	// The phase goes with the offsets it produced. A mode that stops panning has no pass to be
	// halfway through, so a later return to a panning mode starts from the near end rather than
	// from where the previous one happened to stop.
	// (M3 Task 5: the same reasoning covers a change *between* two panning layouts -- a `Scroll`
	// band's horizontal position means nothing on a column of a vertical `Stripes` panel -- which
	// is why applyMode() now calls this on every mode change and not only into `Static`.)
	state.scrollPhase = 0.0;
	// (M4 Task 6: the marquee's pass is a pixel displacement rather than a phase, so it is reset
	// beside the phase it replaces. Starting a new layout from the near end is the same rule the
	// fraction follows: a mode change must not inherit a position measured on the old cell size,
	// and the pingpong latch has no meaning across two different `total`s.)
	state.marqueeOffset = 0.0;
	state.marqueePingpongSign = 1.0;
	state.marqueeLegDistance = 0.0;
}

void MascotController::onScrollTick()
{
	if (!this->scrollCanTick()) {
		// The belt to updateScrollDriver()'s braces: a host can stop being visible without an event
		// this class sees (a parent hidden underneath the host is one), and the rule is that a
		// panel nobody can see costs nothing. Stop, rather than keep timing out into discarded
		// work.
		this->stopScrollDriver();
		return;
	}
	if (!this->scrollClock.isValid()) {
		// A driver started by a path that did not arm the clock: this tick only starts it, so the
		// first delta is measured from now rather than from a stale start.
		this->scrollClock.start();
		return;
	}
	// restart() answers the milliseconds since the previous start and begins a new measurement in
	// the same call, so the delta is the one frame's, never the whole run's.
	const qint64 elapsedMs = this->scrollClock.restart();
	// A clock that went backwards -- a suspended machine, an adjusted system clock -- must not run
	// the pan in reverse: dt 0 is the honest answer, and the next tick measures again.
	if (elapsedMs <= 0)
		return;
	const double dtSeconds = static_cast<double>(elapsedMs) / 1000.0;
	// M3 Task 5: one tick's pan in pixels at the configured rate -- the step the panel's widest
	// band takes, since the phase is normalised against that band's travel (Task 4). For that band
	// a tick advances by more than its whole travel exactly when its travel is shorter than this,
	// and the drawn position would be effectively random per frame rather than moving. Such a band
	// takes the still branch below instead (the same answer Ruling M3-6 gives content that fits its
	// band, for the adjacent case where it barely overflows). A band narrower than its panel's
	// widest shares that phase, so its own step is proportionally smaller and this test is
	// conservative for it; the error is toward standing still on a travel measured in pixels,
	// which is invisible at the default 40 px/s.
	// (M3 Task 5 fix: the test below no longer uses this nominal step. It compares the phase
	// advance the tick actually applied -- `scrollSpeed * elapsedMs / referenceTravel`, i.e. the
	// measured dt over the panel's reference travel -- against one whole travel, so a narrow band
	// on a fast tick is no longer frozen and a stalled frame that really did jump a pass is
	// caught. The paragraph above is kept as the record of the first version.)
	// M3 Task 6A: the changes this tick's seams take out of their bands. They run after the loops
	// below, never inside them: a commit is content work (the cadence's queued swap), and it must
	// not be able to run while this tick still holds the `state`, `surface` and band pointers of
	// the loop it was found in. Running them afterwards is what makes applyMode()/syncBands()/
	// stop()/releaseSurface() from a commit harmless instead of a use-after-free: by then nothing
	// in this function is iterating, and applyMode() still re-queues itself while a commit runs.
	std::vector<MascotBand::WrapCommit> wrapCommits;
	// M4 Task 2: one pass per panel that has a surface, the background included -- the phase, the
	// layout and the direction are all panel-typed, so this is the same tick applied to the layer's
	// own bands. The log text is namespaced by panelLabel(), which spells the two pair panels
	// exactly as sideLabel() did, so a columns-only run's lines are unchanged.
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		const PanelLayout& layout = this->layoutFor(panel);
		// M4 Task 2: a panel whose own layout is `Static` has no pass to advance. The pair can never
		// reach this loop in `Static` (the driver does not run for it), but the background can: the
		// driver may be alive for a panning column pair while the layer draws this task's
		// one-full-window-band `Static` shape, and a tick must not move it.
		if (layout.mode == MascotMode::Static)
			continue;
		const bool vertical = this->scrollPanIsVertical(panel);
		const bool reverse = this->scrollDirectionReverses(panel);
		const bool pingpong = layout.scrollPingpong;
		PanelState& state = this->stateFor(panel);
		MascotSurface* surface = state.surface.data();
		if (!surface || state.bands.empty())
			continue;
		// M4 Task 6: the background's `Scroll` is the seamless marquee. Its cells are translated
		// by a pixel offset, so this is not the phase path below: the rectangle is the property
		// that moves, and the offset is kept in pixels so `speed` stays a px/s rate across resizes.
		// A background `Scroll` with only one funded band is the static-cell case and takes no
		// phase path either -- its content must not pan inside the full-window cell.
		if (panel == MascotPanel::Background && layout.mode == MascotMode::Scroll) {
			if (!this->backgroundMarqueeActive(panel))
				continue;
			const QRectF frame(surface->rect());
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
			// `|offset|` advancing by one `total` is one complete cycle: the sign flips there, and
			// the modulo placement makes the flip seamless. The leftover distance is kept so a
			// frame that straddles the fold does not lose the part of the leg after it.
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
				// The one-write path for a moving cell: the rectangle *is* the pan, so a changed
				// rect is written and an unchanged one is not -- the same per-tick rule the offset
				// path below keeps. No setSlotScrollOffset() is involved on this path at all.
				const QRectF rect = marqueeBandRect(frame, count, i, offsetAfter, vertical);
				if (rect != band->rect())
					band->setRect(rect);
				if (band->hasWrapCommit()) {
					// A cell's seam is its unwrapped position crossing a multiple of `total`: that
					// is the frame it leaves the window, which is when the next clip may take its
					// place. Both travel senses are the same comparison.
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
		// One phase per panel (the plan's step 1): the bands of a panel are one pass, and Task 5
		// staggers them with per-band offsets instead of a second clock. The pass is measured
		// against the panel's widest band, because the speed is a px/s rate and that is the longest
		// travel the panel has; a panel whose bands all fit their content has no pass at all, so
		// its phase does not advance and every band below takes the still branch.
		double referenceTravel = 0.0;
		for (const auto& owned : state.bands)
			referenceTravel = std::max(referenceTravel, this->scrollTravel(panel, *owned));
		const double phaseBefore = state.scrollPhase;
		double phaseRaw = phaseBefore;
		double phaseDelta = 0.0;
		if (referenceTravel > 0.0 && layout.scrollSpeed > 0) {
			// px/s -> travel lengths/s -> this tick's slice of a travel. A speed of 0 is not "pan
			// infinitely slowly", it is "do not pan": the phase is left alone so that turning the
			// speed back up resumes where the band was instead of having moved invisibly.
			const double delta = static_cast<double>(layout.scrollSpeed) * dtSeconds / referenceTravel;
			phaseDelta = delta;
			phaseRaw = phaseBefore + delta;
			state.scrollPhase = advancedScrollPhase(phaseBefore, delta);
		}
		// M3 Task 5: the aliasing test. One tick advances the panel's shared phase by `phaseDelta`
		// travel lengths; at a whole travel or more every band's fraction wraps more than once in
		// the frame, so the drawn positions are effectively random instead of moving, and the bands
		// below take the still branch. Both terms are the quantities the phase is actually advanced
		// by -- this tick's measured dt and the panel's reference travel -- so a stalled frame that
		// really did jump a whole pass is caught, and a narrow band on a fast tick that moves a
		// fraction of its own travel is not mistaken for an aliasing one.
		const bool phaseAliases = referenceTravel > 0.0 && phaseDelta >= 1.0;
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			const double travel = this->scrollTravel(panel, *band);
			// M3 Task 5: a positive travel shorter than one tick's step is the aliasing case, and
			// it takes the still branch too. It cannot pan at frame rate, so it is centred and
			// left there -- the same answer, and the same once-per-layout line (with a wording
			// that names the travel instead of the content), as content that fits its band.
			// A queued wrap-point commit has no seam on such a band either, exactly as for a band
			// with travel 0; the caller that queued it (Task 6) has to handle "no pass is running".
			// (M3 Task 5 fix: the comparison is no longer the band's travel against a nominal tick.
			// A tick that advances the panel's shared phase by a whole travel or more makes every
			// band's fraction wrap more than once, so the test is `phaseAliases` above -- measured
			// from the dt this tick actually saw and the panel's reference travel -- and the band's
			// own travel only has to be positive. The paragraph above is kept as the record of the
			// first version.)
			// M3 Task 5: a band that can pan still takes the still branch while the panel's phase
			// aliases (phaseAliases above): one frame would move it by more than a whole travel, so
			// it is centred and left there -- the same answer, and the same once-per-layout line
			// (with a wording that names the tick instead of the content), as content that fits its
			// band. A queued wrap-point commit has no seam on such a band either, exactly as for a
			// band with travel 0; the caller that queued it (Task 6) has to handle "no pass is
			// running".
			const bool travelAliases = travel > 0.0 && phaseAliases;
			if (travel <= 0.0 || travelAliases) {
				// Nothing to pan: the band is centred and still (Ruling M3-6), and per tick it
				// writes nothing at all. The one write it can owe is the transition off a previous
				// offset -- a band whose content became narrower than its band would otherwise be
				// re-centred by the surface's mirrored offset and stand still *off-centre* -- and
				// that write happens once because the record below then reads 0.
				if (band->scrollOffset() != 0.0) {
					surface->setSlotScrollOffset(band->slotIndex(), 0.0, 0.0);
					band->setScrollOffset(0.0);
				}
				// Said once per band per mode change: it is a statement about the content and the
				// layout, not about a frame, so repeating it at tick rate would bury the lines the
				// gate reads (the M2 rate-limit pattern). Only a band that has content can make the
				// statement -- a band still waiting for its first pick also has travel 0 (an empty
				// slot's painted size is its own rectangle), and letting that consume the latch
				// would silence the line for the narrow picture that is the actual case.
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
			// The configured direction and the band's own sign (Task 5's alternate) are one answer:
			// two reversals cancel, exactly as two mirror flips would.
			const double offset = scrollOffsetFor(fraction, travel, reverse != this->scrollReversed(panel, *band));
			// One property write per band per tick, and none at all when the offset has not moved:
			// the band's record is the comparison, and setSlotScrollOffset() writes only the axis
			// that differs from the mirrored one. The other axis is handed 0 so that a band that
			// changes axis cannot leave a stale pan behind, and the surface filters the unchanged
			// one out.
			if (offset != band->scrollOffset()) {
				surface->setSlotScrollOffset(band->slotIndex(),
					vertical ? 0.0 : offset, vertical ? offset : 0.0);
				band->setScrollOffset(offset);
			}
			if (band->hasWrapCommit()) {
				// A seam is where this band's own pass crosses a whole travel: the wrap point in
				// wrap mode, and a turn in pingpong (the fraction reverses there, but the phase
				// still crosses the same boundary). Both are hard repositionings or reversals of
				// the drawn picture, which is why a queued content change rides one -- the
				// picture changes at the one frame where it is already moving anyway (Ruling
				// M3-5: a wrap is a reposition, never a fade).
				const bool seam = std::floor(phaseRaw + offsetPhase) != std::floor(phaseBefore + offsetPhase);
				if (seam) {
					// Taken here, run after the loops: one seam can never run the same change
					// twice, and the change cannot touch the vectors it was found in while they
					// are still being walked (M3 Task 6A).
					MascotBand::WrapCommit commit = band->takeWrapCommit();
					if (commit)
						wrapCommits.push_back(std::move(commit));
				}
			}
		}
	}
	// M3 Task 6A: the seams' changes, now that no band, surface or state pointer from the loops
	// above is in use. A commit that stopped the controller ends the run rather than calling the
	// remaining changes with bands that no longer exist; a commit that only re-queued a layout
	// change leaves `running` alone and the rest still run.
	for (MascotBand::WrapCommit& commit : wrapCommits) {
		if (!this->running)
			break;
		++this->wrapCommitDepth;
		commit();
		--this->wrapCommitDepth;
	}
}

void MascotController::nextImage(MascotSide side, bool updateHighlight)
{
	// M3 Task 6A: this is the policy band's entry point, so it names band 0; a `Stripes` band
	// advances through pickFor() with its own index (or nextBandImages() when the pair moves).
	this->pickFor(this->stateFor(panelOf(side)), panelOf(side), 0, updateHighlight);
}

void MascotController::nextImages(bool forcedMirror)
{
	// M3 Task 6A: the pair swap is one band index at a time now. A one-band panel's pair *is* its
	// band 0, so `Static` and `Scroll` run exactly the code this method ran before, one band deep.
	this->nextBandImages(0, forcedMirror);
}

void MascotController::nextBandImages(int bandIndex, bool forcedMirror)
{
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
		// M2: a video pick cannot be copied the way an image can. An image's media is the pick
		// (the decode happens later but the media already names the file and its geometry); a
		// video's media does not exist until a frame of it reaches the sink, so what the pair has
		// to share is the file *and the start offset*, and the offset is only known once the left
		// panel's load reports it. This branch therefore either waits for that report or starts
		// the right panel from the media the left panel is already showing.
		// (M3 Task 6A: every sentence below is about band `bandIndex` of both panels now. The
		// offset a copy waits for is the left band's own first frame, so a frame from band 2 can
		// never satisfy the copy owed by band 0 -- and the copy pending flag is the right band's,
		// on that same index.)
		if (leftBand->requestIsVideo() || leftBand->playback().videoPickRetryPending) {
			MascotVideoSlot* leftVideo = leftBand->video();
			if (leftVideo && leftVideo->isBusy()) {
				// Still in flight: the right panel's copy is started from the left panel's
				// firstFrame(), which carries the offset the pair has to agree on.
				this->preparePick(this->rightState, bandIndex, false);
				rightBand->playback().mirrorCopyPending = true;
				return;
			}
			if (leftBand->playback().videoPickRetryPending) {
				// The left panel's pick is waiting for its QML delegate. That retry will start a
				// load, and that load's first frame is where the pair's offset exists -- so the copy
				// is armed now. Without this the pair would be silently unpaired for a whole clip:
				// under the mirror key nothing else ever advances the right panel.
				this->preparePick(this->rightState, bandIndex, false);
				rightBand->playback().mirrorCopyPending = true;
				return;
			}
			if (leftBand->media().type == MascotMedia::Video && !leftBand->media().path.isEmpty()) {
				// Already showing: the offset is committed, so the copy needs no report.
				this->startMirrorVideo(bandIndex, leftBand->media().path,
					leftBand->media().startOffsetMs);
				return;
			}
			// Neither loading nor showing: the left panel's pick produced nothing (its players
			// are gone, or it failed before a frame). Say nothing and let the next swap re-pair.
			return;
		}
		if (!left.path.isEmpty()) {
			// Both panels show the same media, each drawn by its own side rule, and the right
			// panel gets it from the commit that just happened: no request, no decode.
			this->preparePick(this->rightState, bandIndex, false);
			this->showMedia(this->rightState, MascotPanel::Right, left, false, true, bandIndex);
			return;
		}
		// The left panel's pick is still in flight (H1): copy the path it is about to show, not
		// the one it still shows, and the single delivery fills both panels.
		if (leftBand->requestPath().isEmpty())
			return;
		this->preparePick(this->rightState, bandIndex, false);
		this->requestPath(this->rightState, leftBand->requestPath(), bandIndex);
	}
	else {
		this->pickFor(this->rightState, MascotPanel::Right, bandIndex, false);
	}
}

void MascotController::flipSide(MascotSide side)
{
	PanelState& state = this->stateFor(panelOf(side));
	MascotBand* band = state.band(0);
	// The retired renderer's flip was a no-op on an empty pixmap, so flipping a panel that shows
	// nothing is not a state change here either.
	if (!state.surface || !band || band->media().path.isEmpty())
		return;
	state.flipToggled = !state.flipToggled;
	// (M3 Task 6B: the parity is the panel's, so every band of the panel is written with it --
	// a `Stripes` panel's other bands used to keep the old parity until their next commit, and a
	// video band that refused a beat's swap has to flip exactly like an image band. The write is
	// the same mirrored property an image band gets: no file is re-opened, re-seeked or reloaded,
	// and a band showing nothing is skipped as it is above.)
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
	// MainWindow::setLeftMascot()/setRightMascot()'s rule, unchanged: a file from the `right`
	// directory is drawn mirrored on the left panel and one from `left` on the right panel.
	const QString parent = utils::getParentDirectoryName(path, 1);
	if (side == MascotSide::Left)
		return parent == QStringLiteral("right");
	return parent == QStringLiteral("left");
}

color_area MascotController::colorFor(MascotSide side, bool weighted)
{
	const PanelState& state = this->stateFor(panelOf(side));
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
	PanelState& state = this->stateFor(panelOf(side));
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
	if (event->type() == QEvent::Resize) {
		// The host gets its width from the layout after applyMascotWidth() returns, so the
		// surface is the only object that knows when its own rectangle is final.
		if ((this->leftState.surface && watched == this->leftState.surface.data())
			|| (this->rightState.surface && watched == this->rightState.surface.data())) {
			this->updateLayout();
		}
		// M4 Task 2: the background surface is in no layout, so nothing sizes it but updateLayout(),
		// and its geometry is derived from the host's rectangle -- which is final only when the
		// host's own resize event arrives, after MainWindow::event() has already asked for a
		// layout. Watching the host is therefore the background's equivalent of the second pass
		// the columns get from their surfaces' own resize events.
		if (this->backgroundState.surface && watched == this->backgroundHost.data())
			this->updateLayout();
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
			// M3 Task 4: the pan obeys the same three events the players do. It is a separate
			// reconciliation from applyVideoPaused() only because the driver has a fourth reason
			// to be idle (no mode with a pan in it) that the players know nothing about.
			this->updateScrollDriver();
			break;
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::WindowDeactivate:
		case QEvent::WindowStateChange:
			this->applyVideoPaused();
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
	// M4 Task 2: with the columns off the pair is not built at all. The background keeps the
	// controller running in that state, so a pair of hidden, never-seeded surfaces behind it would
	// be idle Qt Quick cost and an empty pair that a later start() (the columns coming back) would
	// mistake for one already being on. With the columns on this is the two builds it always was.
	if (this->columnsEnabled) {
		if (!left)
			left = build(this->leftHost.data());
		if (!right)
			right = build(this->rightHost.data());
	}
	this->setSurfaces(left, right);
}

void MascotController::refreshBackgroundBackdrop()
{
	// M4 Task 6: the layer's holes -- the letterbox bars of a `Fit` cell and the empty surface of a
	// layer with no band -- would otherwise be the quick widget's opaque black. Painting the
	// application's own window colour behind every slot makes those pixels indistinguishable from
	// the window, which is the point: the layer becomes scenery, not a black sheet. Only the
	// background surface is ever handed this; the pair and the columns keep the QML defaults.
	MascotSurface* surface = this->backgroundState.surface.data();
	if (!surface || !this->App)
		return;
	surface->setBackdrop(this->App->palette().color(QPalette::Window), true);
	// M5 Task 8: `MM_MASCOT_FLAT=#RRGGBB` replaces the layer with one flat colour -- the backdrop
	// painted, the slots faded out -- so the veil's blend can be measured against a known value instead
	// of against a clip that is picked at random per run. Kept in the build rather than re-added for one
	// run: it is the only way the harness can assert the veil's arithmetic (see `m5-veil`), and the
	// milestone paid for that lesson once already, when the equivalent M4 hook was deleted and the
	// measurement had to be rebuilt from nothing. It is inert unless the variable is set.
	const QColor flat = QColor::fromString(qEnvironmentVariable("MM_MASCOT_FLAT"));
	if (flat.isValid()) {
		surface->setBackdrop(flat, true);
		surface->setSurfaceOpacity(0.0);
	}
}

void MascotController::refreshColumnsVeil()
{
	if (!this->App || !this->App->config)
		return;
	const double veilOpacity = std::clamp(
		this->App->config->get_double(QStringLiteral("mascots_background_opacity"), 0.85), 0.0, 1.0);
	const bool above = this->App->config->get(QStringLiteral("mascots_columns_z"))
		.compare(QStringLiteral("Below"), Qt::CaseInsensitive) != 0;
	// With the layer off there is no veil to be under, so the pair keeps its ordinary drawing whatever
	// the key says -- otherwise a config with columns Below and no layer would come up with a backdrop
	// nobody asked for.
	const bool veiled = this->backgroundEnabled && !above;
	const QColor colour = this->App->palette().color(QPalette::Window);
	for (PanelState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		state->surface->setBackdrop(colour, veiled);
		state->surface->setSurfaceOpacity(veiled ? (1.0 - veilOpacity) : 1.0);
	}
}

void MascotController::updateBackgroundVisibility()
{
	MascotSurface* surface = this->backgroundState.surface.data();
	if (!surface)
		return;
	// A layer with no funded band has nothing to draw, and "nothing" from this widget is opaque
	// black rather than the window behind it: without `WA_AlwaysStackOnTop` the quick widget blits
	// its offscreen buffer as an opaque rectangle (MascotSurface's constructor records the
	// measurement). Edge-to-edge video hides that entirely, which is why the layer can take the
	// trade at all -- but an empty layer would put a black sheet behind the rows at (1 - alpha),
	// and the list would read as darker than the app's own background for no reason the user asked
	// for. So existence stays syncBackgroundSurface()'s question and this is the visibility one.
	const bool drawable = this->backgroundBandCount() > 0;
	if (surface->isVisible() != drawable)
		surface->setVisible(drawable);
}

void MascotController::syncBackgroundSurface()
{
	// The layer's whole invariant, in one place: the surface exists if and only if this controller
	// is running and `mascots_background` is on. Everything that builds or drops one goes through
	// here -- the enable and disable paths, the settings Apply -- so no other path has to know it,
	// and a background that is off costs neither a surface nor a decode.
	const bool wanted = this->running && this->backgroundEnabled;
	if (!wanted) {
		if (this->backgroundState.surface)
			this->destroyBackgroundSurface();
		return;
	}
	if (this->backgroundState.surface)
		return;
	this->createBackgroundSurface();
}

void MascotController::createBackgroundSurface()
{
	QWidget* host = this->backgroundHost.data();
	if (!host || !this->cache)
		return;
	// No layout on the host: the panel path gives its surface a zero-margin layout because the
	// host is a fixed column the layout sizes, while the background is positioned by hand from the
	// host's rectangle in updateLayout(). A layout here would fight that write for the geometry.
	// The third argument is the layer's whole reason for existing: `false` keeps the quick widget in
	// ordinary stacking order so `lower()` below really puts it beneath the UI. With the panel's
	// default (`true`) the QQuickWidget is drawn above every other widget in the window regardless of
	// stacking order and the wallpaper covers the list -- measured, 50/255 of luminance of
	// difference across the list region while the log said the layer was drawing.
	auto* surface = new MascotSurface(this->cache, host, false);
	this->backgroundState.surface = surface;
	// The bands are the funded prefix bandCount() derives from backgroundPlayers, and
	// configureSurface() builds the slots, the players and the (pair-only) connections.
	this->configureSurface(this->backgroundState, MascotPanel::Background);
	// Geometry, stacking order and visibility, in that order. `lower()` puts the layer beneath the
	// layout's widgets -- it is the window's backmost child -- and a child created after its parent
	// was shown is created hidden, so show() is what makes the layer appear on the very first
	// enable (the same reason the panel path shows its surface).
	surface->setGeometry(host->rect());
	surface->lower();
	surface->show();
	// (M4 review fix: the layer must not eat clicks. Its quick child already passes the mouse
	// through, but that attribute does not stop the wrapper itself from being the widget under the
	// cursor in the parts of the central widget no sibling covers -- which, for a window-sized
	// child, is most of the empty area. A background is scenery: nothing it covers loses its events.)
	surface->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	// M4 Task 2: the host's resize is what makes the layer's final rectangle, since the surface is
	// in no layout (see the resize branch of eventFilter()). Installed here and removed with the
	// surface, so a controller that never runs the layer watches nothing.
	host->installEventFilter(this);
	// The latches describe the layout this surface was built from, so the next applyMode() compares
	// against what is actually drawn rather than against a default.
	this->backgroundLayout.appliedMode = this->backgroundLayout.mode;
	this->backgroundLayout.appliedOrientation = this->backgroundLayout.orientation;
	this->backgroundLayout.applied = true;
	// The opacity the last Apply asked for, so a layer built by that Apply comes back at the
	// configured presentation instead of at the QML default. Zero is applied like any other value:
	// it is presentation, and no band decision reads it.
	// (M5 Task 8: the layer is built at full strength and stays there -- the veil over it carries the
	// configured alpha, and a fade here would be a second one.)
	// M4 Task 6: the app's own window colour behind the layer, enabled from the moment the surface
	// exists -- including the zero-player case, where the whole layer is one backdrop-coloured
	// sheet that updateBackgroundVisibility() then hides for the reason that predates this task.
	this->refreshBackgroundBackdrop();
	// The slot rectangles are written from the surface's geometry, so the layout is settled here:
	// the enable path reaches this before any window resize (applyConfig() also calls updateLayout()
	// later in the same turn, which is idempotent). Without it a surface created after
	// createSurfaces() would draw its first frames at a rect nobody had written.
	this->updateLayout();
	// (M4 review fix: `show()` above is the creation-time visibility the comment there describes;
	// whether the layer should be showing *now* is the funded-prefix question, and a surface built
	// by an Apply that left the layer no player must come up hidden rather than as a black sheet.)
	this->updateBackgroundVisibility();
}

void MascotController::destroyBackgroundSurface()
{
	if (!this->backgroundState.surface)
		return;
	// The host filter installed by createBackgroundSurface() goes with the layer it was watching.
	if (QWidget* host = this->backgroundHost.data())
		host->removeEventFilter(this);
	// The pair's order, for the pair's reason: the players draw into QVideoSinks that belong to
	// the surface's QML delegates, so they are shut down while those delegates still exist.
	// clearSide() then drops the slots, the requests and the pins, and releaseSurface() destroys
	// the players that remain and deletes the surface.
	for (auto& owned : this->backgroundState.bands) {
		if (owned->video())
			owned->video()->shutdown();
	}
	this->clearSide(this->backgroundState);
	this->releaseSurface(this->backgroundState);
}

void MascotController::configureSurface(PanelState& state, MascotPanel panel)
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
	this->syncBands(state, panel);
	// The image path's mode (M1): the media is scaled to the panel's height with its aspect
	// ratio kept, which is what the old renderer's scaled(width, height, KeepAspectRatio)
	// produced -- a tall panel keeps a narrow image's side margins and crops a wide one
	// horizontally. The surface owns that geometry (amendment R); the controller only picks the
	// mode. A later milestone that shows video picks its own mode here.
	// M2: that milestone is this one, so the write moved to applyDisplay(), which picks the mode
	// per media type -- and both types pick HeightFit (§8 M2 ruling), so what is drawn is the
	// mode this comment describes. The comment stays as the record of why.
	// M4 Task 2: the surface-wide mirror default is the pair's rule. A background surface never
	// mirrors (mirrorParityFor() is false for it), so it starts from false and keeps it -- a slot
	// created by a later setSlotCount() would otherwise inherit the columns' default and draw the
	// layer flipped until its next commit. The pair's answer is unchanged.
	MascotSide pairSide = MascotSide::Left;
	const bool hasPairSide = this->pairSideOf(state, &pairSide);
	surface->setMirror(hasPairSide ? this->mirrorEnabled() : false);
	// M3 Task 2: one mirror per band, from the media that band already carries. setMirror() above
	// is only the bulk default a slot created by setSlotCount() starts from; these calls are what
	// re-apply the per-band flip parity a surviving band had before the surface was rebuilt (a
	// single-band panel still makes exactly the one call it made before bands were counted).
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i)
		this->applyMirror(state, panel, state.bands[static_cast<std::size_t>(i)]->media().path, i);
	surface->installEventFilter(this);
	// M4 Task 1: a click and a drop are pair-only policy, so these connections exist only for the
	// two states that have a pair side. Which side that is comes from the state itself rather than
	// from a panel -> side conversion; a background surface (Task 2) owns neither state and wires
	// neither signal.
	if (hasPairSide) {
		connect(surface, &MascotSurface::clicked, this,
			[this, pairSide](int, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
				emit this->clicked(pairSide, button, modifiers);
			});
		connect(surface, &MascotSurface::imageDropped, this, [this, pairSide](int, QString path) {
			this->showDroppedImage(pairSide, path);
		});
	}
	// M2: the player half of this panel, when the media setting and the decoder budget allow one.
	// The sinks stay unbound until the first swap (see createVideoSlot).
	// (M3 Task 1: createVideoSlot() calls the band's ensureVideo(), which binds them when the
	// delegate already exists; the sentence above is why it does not wait for them.)
	this->ensureVideoSlots();
}

void MascotController::releaseSurface(PanelState& state)
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

void MascotController::clearSide(PanelState& state)
{
	// M3 Task 2: every band of the panel, not only its policy band. A `Stripes` panel shows a
	// picture per band, so each band's path is an owner field the pin invariant has to see gone
	// before the pins are re-evaluated -- and each band's slot and players are part of "the panel
	// is cleared". In `Static` this is one band and the statements below are the ones this method
	// always ran.
	QStringList previousShown;
	for (auto& owned : state.bands)
		previousShown.push_back(owned->media().path);
	// M3 Task 6A: the candidate is the band's now, so every band's speculative path is an owner
	// field the pin invariant has to see gone before the pins are re-evaluated.
	QStringList previousCandidates;
	for (auto& owned : state.bands)
		previousCandidates.push_back(owned->playback().candidate.path);
	// M2: a cleared panel must not keep a clip playing into a slot nobody draws, and `clearSlot()`
	// deliberately leaves the video state alone (Task 1's interface fact), so both halves are done
	// here: the players are stopped and their outputs hidden.
	// (M3 Task 1: the outputs are hidden by the band's clear() below, which also empties the slot
	// and forgets the media; stopping the players is still here, because whether they live is the
	// caller's decision.)
	// (M3 Task 2: "the players" is every band's players for the same reason the paths above are.)
	// (M3 Task 6A: the cadence is every band's too. A band's timer left armed would advance a panel
	// nobody draws, so the timers stop here and the bands' own clear() stops them again.)
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
	// Amendment H2: the owner fields are gone before the pins are re-evaluated, so a path that
	// only this panel referenced is unpinned and one another panel still references is not.
	// A pinned entry is never evicted, which is why a missed unpin is invisible until the
	// cache has grown past every bound.
	for (const QString& path : previousShown)
		this->syncPin(path);
	for (const QString& path : previousCandidates)
		this->syncPin(path);
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
	// (M3 Task 6A: each band's *candidate* is one of those fields too -- it is a decoded media the
	// band is about to show, so it is pinned exactly as band 0's candidate always was.)
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
	band->applyFill(this->effectiveFillMode(panel));
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
	// (M3 Task 6A: the timer is the *band's* now, and this stops the one belonging to the band
	// that just committed an image. Band 0's image commit and a stripe's are therefore independent
	// cadences: neither can stop the other, which is what the Task 5 note above was waiting for.)
	if (QTimer* timer = band->clipTimer())
		timer->stop();
	// `mascots_center_content` off means "centre the whole image, not its content", which is
	// the surface's own shift set to zero. The media write above resets any previous
	// override, so this has to come after it (amendment R: the surface owns the arithmetic,
	// this only says whether to use it).
	if (!this->centerContentEnabled())
		band->setContentOffset(QPointF(0.0, 0.0));
}

bool MascotController::mirrorParityFor(MascotPanel panel, const QString& path) const
{
	// M4 Task 1: the parity a per-panel commit is written with. The pair panels keep the
	// side-directory rule exactly as flipForSide() answers it; the background has no side
	// directory rule of its own and no pair to mirror with, so it is never flipped.
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
	if (state.mirrorOverrideEnabled)
		mirrored = state.mirrorOverrideValue;
	else if (!path.isEmpty())
		mirrored = this->mirrorParityFor(panel, path);
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

void MascotController::requestPath(PanelState& state, const QString& path, int bandIndex)
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
	// (M3 Task 6A: it is the *band's* now -- the pair is paired per index -- so an image request on
	// band i clears band i's copy and leaves every other band's copy alone.)
	band->playback().mirrorCopyPending = false;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

MascotMedia MascotController::pickFor(PanelState& state, MascotPanel panel, int bandIndex,
	bool updateHighlight)
{
	if (!this->running || !state.surface)
		return MascotMedia();
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return MascotMedia();

	// M2: what kind of media this pick is, decided before the candidate block below because a
	// video pick has no candidate to be served from (its media is the first frame) and because an
	// image candidate left over from an image-mode pick must not be shown over a video panel.
	const MascotMedia::Type type = this->pickType(state, panel);
	if (type == MascotMedia::Video) {
		switch (this->pickVideo(state, panel, bandIndex, updateHighlight)) {
		case VideoPick::Started:
			return MascotMedia();
		case VideoPick::Skip:
			// A load is already in flight, or the panel is about to retry its pick. Either way
			// this swap is done: falling through would put an image over a video panel.
			return MascotMedia();
		case VideoPick::Fallback:
			// M4 Task 2: the background has no image path to fall back to. An empty video pool (or
			// no player left) means the layer stays blank, said once per episode rather than once
			// per beat; it must never be filled with a still.
			if (panel == MascotPanel::Background) {
				if (!this->backgroundEmptyPoolLogged) {
					this->backgroundEmptyPoolLogged = true;
					this->logBackground(QStringLiteral("the video pool is empty; the background has no clip to show and stays blank"));
				}
				return MascotMedia();
			}
			// Video cannot serve this pick -- the pool emptied between the size and the draw, or
			// the player half is gone. The image path below serves it instead of leaving the panel
			// without one, and the pick line under this switch records that it did.
			break;
		}
	}
	if (this->mediaMayBeVideo(panel) && this->App && this->App->debug_mode) {
		// One line per swap while a video configuration is in play, so the gate can see which
		// branch served each pick and how big the pool was. Never written for an Images setting,
		// which is what keeps an images run's log identical to merged master's.
		// (M3 Task 6A: the line names the band when it is not the policy band; band 0's text is
		// unchanged. The rate limit stays the panel's -- the line is about which pool branch served
		// a pick, not about one band.)
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (state.lastTypeLogMs == 0 || (now - state.lastTypeLogMs) >= VIDEO_LOG_INTERVAL_MS) {
			state.lastTypeLogMs = now;
			this->logVideo(QStringLiteral("%1 pick=image pool=%2 media=%3")
				.arg(bandLabel(panel, bandIndex))
				.arg(this->source.videoPoolSize())
				.arg(this->mediaSetting));
		}
	}

	// A ready candidate: the media was chosen and decoded before the swap was due, so committing
	// it is a property write -- no decode, no directory walk and no request on this path. Any
	// older request this panel still had in flight is dropped by showMedia (Important #1), so a
	// late delivery for it can no longer overwrite what is shown here.
	// (M3 Task 6A: the candidate is the band's own, so a stripe's commit consumes the stripe's
	// predecoded media and nothing of band 0's.)
	if (!band->playback().candidate.path.isEmpty()) {
		const MascotMedia media = band->playback().candidate;
		this->preparePick(state, bandIndex, updateHighlight);
		this->showMedia(state, panel, media, true, true, bandIndex);
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

	this->preparePick(state, bandIndex, updateHighlight);
	// Band 0 is the band this pick path serves: the panel's candidate, its speculation and its
	// video sequence are all band 0's (M3 Task 2).
	// (M3 Task 6A: the band this pick path serves is `bandIndex`; its candidate, speculation and
	// video sequence are that band's own, so the sentence above now describes the index the
	// caller passed -- 0 for every pre-mode call site and for the policy band.)
	this->requestPath(state, path, bandIndex);
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

void MascotController::preparePick(PanelState& state, int bandIndex, bool updateHighlight)
{
	// A fresh pick resets what the previous media carried: the side-directory rule is
	// authoritative again, and the theme colour follows this swap only if it was asked for.
	// (M3 Task 6A fix: the four values below are the *panel's*, so only the policy band's pick may
	// reset them -- showMedia() already gates the same ones on band 0. A stripe's pick used to
	// clear a debug drop's mirror override and a pending theme-colour request while band 0's media
	// was still landing, and a stripe commit neither resets nor restores the flip parity. A stripe
	// that starts a new media keeps the panel's parity and leaves the panel's pending highlight
	// alone, which is what the panel-wide fields mean.)
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
	// M3 Task 2: which of the statements below belong to the panel and which to the band. The
	// candidate, the speculation, the flip parity, the mirror pair and the theme colour are the
	// panel's policy and live on band 0; a `Stripes` panel's extra bands are filled outside all of
	// it, so their commits must not consume the panel's predecoded candidate or drop the pair's
	// pending copy. `Static` is always band 0, so this is the path it always ran.
	// (M3 Task 6A: the candidate, the speculation and the mirror-copy wait are the band's now, so
	// every band's commit consumes and arms its own. The flip parity stays the panel's -- a stripe
	// swapping must not clear a flip the user made on the panel -- and the theme colour stays band
	// 0's, because the accent belongs to the panel and four stripes swapping would strobe it.)
	const bool policyBand = bandIndex == 0;

	// Amendment H2: the owner fields change first and the pins are re-evaluated afterwards, so a
	// path that stays referenced -- the candidate becoming the displayed media, or the other
	// panel showing the same file -- is never unpinned even for one statement, which would let
	// eviction take the pixels between the two.
	const QString previousShown = band->media().path;
	const QString previousCandidate = band->playback().candidate.path;
	if (fromReady)
		band->playback().candidate = MascotMedia(); // consumed by this commit
	// (M3 Task 6B: the content's arrival time, which the half-clip floor measures. Written by this
	// one commit path -- an image delivery, a ready candidate, a video's first frame -- so a band
	// that has just changed is young whatever media type it changed to, and requestedAtMs (when
	// the load was asked for) is never mistaken for it.)
	band->playback().committedAtMs = QDateTime::currentMSecsSinceEpoch();
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
	// (M3 Task 6A: every commit drops the *committed band's* prefetched path, not the policy
	// band's: a stripe's speculative media belongs to the stripe.)
	band->playback().prefetchPath.clear();
	// The media that just became visible is pinned *before* the two paths it replaced are
	// re-evaluated: each syncPin() can run eviction, and at the configured floor the entry eviction
	// would then be free to take is exactly this one (M1).
	this->syncPin(media.path);
	// M3 Task 1: the media is recorded and written by the presenter, at the point applyDisplay()
	// used to write it -- after the mirror and the fill mode, so the commit's write order is the
	// one the slot has always seen.
	this->presentMedia(state, panel, media, bandIndex);
	this->syncPin(previousShown);
	this->syncPin(previousCandidate);
	// M3 Task 6A: a commit is this band's content arriving, so both retry budgets start over.
	// A band that was re-picking after failures is done asking until it fails again.
	band->playback().videoFailures = 0;
	band->playback().imageFailures = 0;

	// The next swap is armed after this turn: the swap path must not walk the mascot directory.
	// (M3 Task 6A: the committed band's own next media, through its own index.)
	if (armCandidate)
		this->armSpeculation(panel, bandIndex);

	// M2: the left panel is the one the pair follows, so when it commits an *image* there is no
	// video offset coming for the right panel to copy -- it was armed by a video pick that has
	// since degraded (its sink never appeared, its pool emptied). Dropped here rather than only in
	// onVideoFailed(), because a give-up downgrade never produces a failure to hear about. A video
	// commit is untouched: onVideoFirstFrame() reads the flag after this call.
	// (M3 Task 6A: the copy that is dropped is the right panel's same-index band's, the one a
	// degraded pick armed for this band.)
	if (panel == MascotPanel::Left && media.type == MascotMedia::Image) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = false;
	}

	// M4 Task 1: the accent is pair policy, so a per-panel commit reaches it through the pair
	// state that owns the commit rather than by converting its panel back into a side. The
	// background owns neither pair state and therefore emits no colour.
	if (policyBand && state.highlightOnApply) {
		state.highlightOnApply = false;
		MascotSide pairSide = MascotSide::Left;
		if (this->pairSideOf(state, &pairSide))
			emit this->themeColorChanged(this->colorFor(pairSide, true));
	}

	// M3 Task 6A: the pair rule, extended from one band to N. Band 0's image copy is made by
	// nextBandImages(), which copies the *request* so one delivery fills both panels; a stripe's
	// fill does not go through that pair path, so its commit is mirrored here -- the same file on
	// the right panel's same-index band, drawn by that panel's own side rule. A video commit is
	// not copied here: its offset only exists at the first frame, and onVideoFirstFrame() is where
	// the pair's copy starts.
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
	// Pinned as soon as it exists, and before the candidate it replaces is re-evaluated: a
	// candidate whose pixels were evicted before its swap would put a blank id in the slot, and
	// the unpin below can run eviction.
	this->syncPin(media.path);
	this->syncPin(previous);
}

void MascotController::armSpeculation(MascotPanel panel, int bandIndex)
{
	// Queued rather than called: the pick walks the mascot directory and posts a decode, and
	// neither may sit on the swap path (the swap must stay a property write). Queued to `this`,
	// so a stopped or destroyed controller simply never runs it; speculate() is idempotent, so
	// several commits in one turn collapse into one pick.
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
	// Exactly one deep, per panel: while a candidate is held or its decode is in flight there is
	// nothing to do, and no queue is kept.
	// (M3 Task 6A: per band now -- a `Stripes` panel gives every band the same one-deep lookahead
	// band 0 has always had, so each band's next media is decoded before its swap is due.)
	if (!band->playback().candidate.path.isEmpty() || !band->playback().prefetchPath.isEmpty())
		return;
	// With the mirror key on, the right panel always shows what the left panel shows, so a
	// speculative pick for it would spend a draw nobody displays (and would shift a seeded
	// sequence). It arms itself again the moment it is updated on its own.
	// (M3 Task 6A: the rule is per band index now: a right-panel band the left panel mirrors needs
	// no speculation of its own, while a right-panel band past the left panel's count is its own
	// band and does.)
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
		return;
	// M2: a video panel has no use for a decoded image. Holding one would cost a decode, a cache
	// entry and a pin per clip, and pickFor() would find it waiting when the panel's type
	// changed back -- so the speculation is not armed at all while the panel shows video.
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

bool MascotController::mediaMayBeVideo(MascotPanel panel) const
{
	// Both halves are needed: a budget below two players leaves no panel a player at all, and an
	// images-only setting has no use for the pool. Everything video costs hangs off this answer.
	// (M3 Task 5: "a budget below two players leaves no panel a player" is M2's two-players-per-
	// panel verdict, which only `Static` still uses. A panning mode gives one player per band, so
	// the answer is whether the allocation gave either panel a band with one; a panel that got
	// none falls through pickVideo()'s no-slot branch to the image path.)
	// (M4 Task 2: per panel, and the background asks it about itself. The pair panels keep exactly
	// this answer -- the setting plus an allocation across the pair -- while the background is
	// video-only and answers from its own funded band prefix, which is why the question could not
	// stay global: the pair's answer says nothing about whether the layer may show a clip.)
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount() > 0;
	if (!this->mediaSettingWantsVideo())
		return false;
	return this->videoBandsFor(MascotPanel::Left) > 0
		|| this->videoBandsFor(MascotPanel::Right) > 0;
}

int MascotController::videoBandsFor(MascotPanel panel) const
{
	// M3 Task 5: the panel's player entitlement. `Static` is M2's model: one band, and whether it
	// gets two players (crossfade) or one (the clamp) is applyDecoderBudget()'s decision -- this
	// answer only says the band may own players at all. A panning mode gives each band its own
	// single player, and applyBandBudget() has already spent the budget across the two panels.
	// M4 Task 1: the background has no allocation yet -- applyBandBudget() spends the pair's
	// budget and Task 2 gives the background its own players -- so it owns none.
	// (M4 Task 2: that allocation is applyBackgroundBudget()'s now, and the background answers with
	// its funded prefix -- the same number bandCount(Background) answers, so no band can exist
	// that the budget did not carry. It is deliberately answered before the `mascots_media` guard
	// below: the layer is video-only whatever the columns' setting says.)
	if (panel == MascotPanel::Background)
		return this->backgroundBandCount();
	// M4 Task 2: with the columns off there is no pair to show, so no pair band may own a player --
	// a background-only run must not build idle decoders behind two hidden hosts. A columns-on run
	// answers exactly as before (the setting and the allocation below).
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
	// The allocation is a prefix of each panel's bands, so "may own" is one comparison. The band
	// count is re-checked because a caller between a count change and the next applyBandBudget()
	// could still be asking about an index the panel no longer has.
	return bandIndex >= 0
		&& bandIndex < this->bandCount(panel)
		&& bandIndex < this->videoBandsFor(panel);
}

bool MascotController::videoCrossfades(MascotPanel panel) const
{
	// §7.5: a crossfade is the two-player design, and only `Static` keeps it. `Scroll` and
	// `Stripes` give a band one player, so a swap reloads the output already on screen and there
	// is nothing to blend; they take the hard cut and never consult `mascots_crossfade_ms`. This
	// is the one question fadeInVideo() and setSinglePlayer() both ask, so the two cannot disagree
	// about a mode that has no fade to show.
	// M4 Task 2: per panel, because the background's budget is its own. Its answer is
	// applyBackgroundBudget()'s -- `Static` with both of its players -- and the columns' answer
	// below is unchanged: the setting's mode and the pair's videoMode, and nothing about the
	// background's entitlement can move it.
	if (panel == MascotPanel::Background)
		return this->backgroundCrossfades;
	return this->layoutFor(panel).mode == MascotMode::Static
		&& this->videoMode == VideoMode::Crossfade;
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

MascotMedia::Type MascotController::pickType(PanelState& state, MascotPanel panel)
{
	// M4 Task 2: the background shows video and nothing else. The image pool is portrait stills,
	// and a portrait still stretched across a window is the one look the layer must never draw, so
	// this answer does not consult `mascots_media` at all -- an images-only setting is a statement
	// about the columns.
	if (panel == MascotPanel::Background) {
		// M4 Task 2: the pool is the layer's only source, so a database, filter or row-count change
		// has to invalidate the cached scan here exactly as the pair branch below does -- otherwise
		// a background-only run would keep drawing clips from the library it saw at startup.
		this->invalidateVideoPoolIfStale();
		return MascotMedia::Video;
	}
	if (!this->mediaMayBeVideo(panel))
		return MascotMedia::Image;
	this->invalidateVideoPoolIfStale();
	// The size is what decides, and it answers by scanning the pool if that has not happened yet
	// (MascotMediaSource keeps the scan behind a mutable cache for exactly this call).
	const int pool = this->source.videoPoolSize();
	if (this->mediaSetting.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0 && pool <= 0) {
		// `videoPoolSize() == 0` means "no row passed the filters" *or* "the query failed", and
		// the two are not distinguishable from here: either way the honest answer is images, and
		// the line is rate-limited because it describes the configuration, not an event.
		// (M4 Task 2: the background never reaches this branch returning Image -- its own answer
		// is above -- and its empty-pool case is the single MASCOT-BG line pickFor() writes when
		// its video pick comes back with nothing.)
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

MascotController::VideoPick MascotController::pickVideo(PanelState& state, MascotPanel panel,
	int bandIndex, bool updateHighlight)
{
	MascotBand* band = state.band(bandIndex);
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
			this->logVideo(QStringLiteral("%1 swap waits: a preview tooltip is playing")
				.arg(bandLabel(panel, bandIndex)));
		}
		// The same flag the sink retry sets, for the same reason: this panel is about to start a
		// load, so under the mirror key the other panel's copy has to be armed from that load's
		// first frame rather than left waiting for a commit it will not see on its own.
		band->playback().videoPickRetryPending = true;
		QTimer::singleShot(this->busyDeferMs, this, [this, panel, bandIndex]() {
			if (!this->running)
				return;
			PanelState& panelState = this->stateFor(panel);
			MascotBand* panelBand = panelState.band(bandIndex);
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
			this->pickFor(panelState, panel, bandIndex, bandIndex == 0);
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
	// (M3 Task 6A: the sink asked about is this band's own slot -- the hardcoded slot 0 was the
	// policy band's, and a stripe whose own delegate existed would never get its pick started.)
	MascotSurface* surface = state.surface.data();
	if (!surface || !surface->slotSink(bandIndex, 0)) {
		if (band->playback().videoSinkRetries++ < this->retryAttempts) {
			if (!this->warnedVideoSink) {
				this->warnedVideoSink = true;
				this->logVideo(QStringLiteral("no video sink for the panel yet; retrying the pick"));
			}
			// Recorded for nextImages(): while the pick is waiting, this panel is still the one the
			// mirror pair follows, so the right panel's copy has to be armed from its firstFrame
			// rather than left waiting for a commit that the right panel never sees.
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
		// The delegate never appeared: images are the honest degrade, and the log says why once --
		// once per process, not once per episode, because the retry budget below is reset so a
		// delegate that arrives later gets its own. The counter is reset here so a recovered
		// delegate is not decided by an exhausted budget.
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
	// The clip this swap will show may already be chosen and opened: a preload decides the next
	// path when the previous clip commits, so consuming it here is what makes the swap an opacity
	// change. Drawing again would skip the parked file and shift the seeded sequence by one draw per
	// swap, which is why the parked path wins over a fresh one and why nothing is re-validated
	// against the pool: the pool was read when the preload was made, by this same code path.
	const QString parkedPath = slot->preloadedPath();
	if (!parkedPath.isEmpty()) {
		const qint64 parkedOffsetMs = slot->preloadedOffsetMs();
		// A video pick owns the panel exactly as it does below, for the same reasons.
		if (!band->playback().candidate.path.isEmpty()) {
			const QString dropped = band->playback().candidate.path;
			band->playback().candidate = MascotMedia();
			this->syncPin(dropped);
		}
		band->playback().prefetchPath.clear();
		this->preparePick(state, bandIndex, updateHighlight);
		// The offset is passed rather than left as -1: it is the number that was actually seeked to,
		// and passing it is what lets the log print it and a mirrored pair agree on it.
		this->startVideoLoad(state, panel, bandIndex, parkedPath, parkedOffsetMs);
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
		if (band->playback().videoLoadRetries++ < this->retryAttempts) {
			this->logVideo(QStringLiteral("%1 has no video sink yet; retrying the same load")
				.arg(bandLabel(panel, bandIndex)));
			// The same flag a retried pick sets, because from outside this is the same situation:
			// this panel is about to start a load, so a mirrored pair has to arm the other panel's
			// copy from that load's first frame.
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
		// A parked clip this panel cannot start is not going to become startable: it is dropped so
		// the next swap draws a fresh one instead of retrying a clip whose output has no sink.
		slot->discardPreload();
		// Latched like the pick branch above: one copy per left commit would otherwise repeat this
		// once per clip for as long as the right panel's delegate is missing.
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
	// M4 Task 2: a load that actually starts means the pool had something to give, so the
	// background's empty-pool line is armed again for the next time it has nothing.
	if (panel == MascotPanel::Background)
		this->backgroundEmptyPoolLogged = false;
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
	band->playback().requestedAtMs = QDateTime::currentMSecsSinceEpoch();
	++band->playback().videoSeq;
	// A left-panel video load is what the pair follows, so the right panel's copy is armed here
	// rather than only in nextImages(): a swap served from a preload completes *inside*
	// pickVideo(), which is before nextImages() could have armed anything, and the pair would then
	// never be copied. Arming it at the start of every left video load covers both timings with one
	// rule -- from this moment the right panel owes a copy of this panel's next commit.
	// (M3 Task 6A: the copy is owed by the right panel's *same-index* band, which is the band this
	// load's frame will have to satisfy. A right-panel band past the left panel's count owes
	// nothing, because the left panel has no band for it to mirror.)
	if (panel == MascotPanel::Left && this->mirrorEnabled()) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = true;
	}
	// The `preloaded=` field is how the log tells the two apart: a swap served from a parked clip
	// prints the offset it was parked at and can be paired with the "preloaded" line that preceded
	// it, while `preloaded=no` is a swap that paid for its own load.
	this->logVideo(QStringLiteral("%1 swap #%2 pool=%3 offset=%4 path=\"%5\" preloaded=%6")
		.arg(bandLabel(panel, bandIndex))
		.arg(band->playback().videoSeq)
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

void MascotController::onVideoFirstFrame(MascotPanel panel, int bandIndex, const QString& path,
	int ab, const QSize& frameSize, qint64 offsetMs)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || !band->video())
		return;
	// Only the load this panel is waiting for may be committed: a superseded load's frame (a swap
	// that arrived mid-load) is not this panel's media, exactly as a late image delivery is not
	// (amendment H1).
	// (M3 Task 6A: "this panel" is this band now, and the slot that reported is the band's own --
	// the connection carries the index, and the request record it is matched against is the band's,
	// so a frame from band 2 can never be committed to band 0.)
	if (!band->requestIsVideo() || band->requestPath() != path)
		return;

	const qint64 delta = QDateTime::currentMSecsSinceEpoch() - band->playback().requestedAtMs;
	MascotMedia media;
	media.type = MascotMedia::Video;
	media.path = path;
	media.nativeSize = frameSize;
	media.startOffsetMs = offsetMs;

	// Committed through the same call an image takes, which is what keeps the pins, the mirror
	// reset, highlightOnApply and the theme-colour emission working without a second copy of any
	// of them. `armCandidate` is false: a video panel has no image candidate to arm.
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

	// A mirror pair shares the file *and* the offset, and the left panel's frame is where the
	// offset finally exists -- so this is the moment the right panel can be started on it.
	// (M3 Task 6A: the pending copy is the right panel's same-index band's; band i's frame can
	// therefore only ever satisfy band i's copy.)
	if (panel == MascotPanel::Left) {
		MascotBand* rightBand = this->rightState.band(bandIndex);
		if (rightBand && rightBand->playback().mirrorCopyPending) {
			rightBand->playback().mirrorCopyPending = false;
			if (this->rightState.surface && rightBand->video())
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
	// A crossfade needs two players. In the clamped mode a swap reloads the output that is on
	// screen, so there is nothing to blend and the duration is 0 -- the hard cut the clamp
	// promises -- rather than a second fade path to keep in step with the first.
	// (M3 Task 5: "the clamped mode" is now every mode without the crossfade, and a panning mode
	// never has it whatever the decoder count: one player per band means nothing can blend, so the
	// duration is 0 there too. videoCrossfades() is the single answer both this and
	// setSinglePlayer() read, so a mode cannot pay for a fade it cannot show.)
	const int fade = this->videoCrossfades(panel) ? this->crossfadeMs : 0;
	band->setVideoOpacity(ab, 1.0, fade);
	band->setVideoOpacity(1 - ab, 0.0, fade);
	if (fade <= 0) {
		// No fade to wait for, so the outgoing player's decoder is worth nothing: retired now.
		slot->retireInactive();
		// And only now, with the outgoing player retired, is the idle output the one the *next*
		// swap will use -- so this is the earliest moment a preload can be parked on the right
		// player. Parked before the retire it would be thrown away by it.
		// (M3 Task 6A: only the policy band asks for one, and startPreload() itself only acts in
		// `Static`, where a second player exists. A panning mode's band has one player, so a
		// preload there could never be parked and asking would only draw a file from the pool that
		// nothing could use.)
		if (bandIndex == 0)
			this->preloadNext(panel);
		return;
	}
	// The retirement ends the crossfade: until then the outgoing player is what is still partly
	// visible, which is the only reason its decoder is still open. Queued on `this`, so a
	// controller that was stopped in between simply never runs it.
	QTimer::singleShot(fade, this, [this, panel, bandIndex]() {
		PanelState& panelState = this->stateFor(panel);
		MascotBand* panelBand = panelState.band(bandIndex);
		if (MascotVideoSlot* panelSlot = panelBand ? panelBand->video() : nullptr)
			panelSlot->retireInactive();
		// The same ordering rule as the hard cut above: the preload follows the retirement, because
		// until it has happened the output a preload would pick is the outgoing clip's.
		if (bandIndex == 0)
			this->preloadNext(panel);
	});
}

void MascotController::preloadNext(MascotPanel panel)
{
	// Queued for the same reason armSpeculation() is: the pick reads the pool and the preload posts
	// a load, and neither may sit inside the commit or the fade callback that asked for them. Queued
	// to `this`, so a controller that was stopped or destroyed in between simply never runs it.
	QMetaObject::invokeMethod(this, [this, panel] { this->startPreload(panel); }, Qt::QueuedConnection);
}

void MascotController::startPreload(MascotPanel panel)
{
	// M4 Task 2: the preload stays pair-only. It parks a clip on the idle output of a crossfading
	// column pair, which the background does not have a use for (its cadence is the beat and its
	// clip timer, and a parked clip would hold a decoder the budget did not give it), so the layer
	// is refused here as well as at the call sites that ask.
	if (panel == MascotPanel::Background)
		return;
	if (!this->running || !this->preloadEnabled || !this->mediaMayBeVideo(panel))
		return;
	// M3 Task 6A: a preload needs a second player to park on, and only `Static`'s crossfade has
	// one. `Scroll` and `Stripes` give every band a single player, so `MascotVideoSlot::preload()`
	// refuses to park on the output that is on screen -- the only one -- after the first clip.
	// Asking anyway would draw a file from the pool that nothing could ever use and shift the
	// seeded sequence by one draw per clip, so the decision is made here rather than paying for a
	// preload that cannot exist. `Static` keeps M2's path exactly, one-player clamp included.
	if (this->layoutFor(panel).mode != MascotMode::Static)
		return;
	PanelState& state = this->stateFor(panel);
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
	if (panel == MascotPanel::Right && this->mirrorEnabled())
		return;
	// A missing sink is the delegate being a turn behind. The swap path has its own retry budget for
	// exactly that, so a preload does not compete for it: it simply waits for the next commit.
	// (M3 Task 1: the band's ensureVideo() also binds the sinks on the success path, which is the
	// setSinks() this method used to make just before preload().)
	if (!band->ensureVideo(0))
		return;
	// Which branch serves this pick is the same question the swap asks, and the pool it asks about
	// is the pool this preload will draw from: a panel whose type is not video has nothing to park.
	if (this->pickType(state, panel) != MascotMedia::Video)
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

void MascotController::onVideoPreloaded(MascotPanel panel, int bandIndex, const QString& path,
	qint64 offsetMs, const QSize& frameSize)
{
	if (!this->running || path.isEmpty())
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || !band->video())
		return;
	// The gate's evidence that a swap was served from a preload is this line followed by a
	// `swap #n ... preloaded=yes` for the same path and offset.
	this->logVideo(QStringLiteral("%1 preloaded path=\"%2\" offset=%3ms %4x%5")
		.arg(bandLabel(panel, bandIndex))
		.arg(path)
		.arg(offsetMs)
		.arg(frameSize.width())
		.arg(frameSize.height()));
	// The pair shows one file at one offset, and the offset exists only now that the left panel has
	// reported it -- so the right panel's copy of the pair is parked here rather than chosen by its
	// own preload. Both panels are then parked on the same frame, and the swap is two flips in the
	// same turn instead of one panel waiting for the other's first frame.
	// (M3 Task 6A: the copy is parked on the right panel's same-index band. Only band 0 preloads --
	// the panning modes have one player per band, so nothing there can be parked -- which is why
	// every report this method sees names band 0.)
	if (panel != MascotPanel::Left || !this->mirrorEnabled())
		return;
	PanelState& right = this->rightState;
	MascotBand* rightBand = right.band(bandIndex);
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

void MascotController::startMirrorVideo(int bandIndex, const QString& path, qint64 offsetMs)
{
	PanelState& right = this->rightState;
	MascotBand* rightBand = right.band(bandIndex);
	if (!right.surface || !rightBand || !rightBand->video() || path.isEmpty())
		return;
	// The pair shows one media, so the right panel's pick *is* the left panel's: same file, same
	// offset. A second random start would show two different moments of the same clip, which is
	// the one thing the mirror key exists to prevent.
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
		++band->playback().videoFailures;
	}
	// The other panel may be waiting for this panel's offset to start its copy of the pair. It
	// will never arrive now, and the next committed media re-pairs them.
	// Cleared whether or not this failure was the left panel's own swap: a left panel that just
	// failed anything is not going to commit a pair the right panel could copy, and leaving the
	// flag set would park a copy that waits for a report which is not coming.
	// (M3 Task 6A: the copy that is dropped is the right panel's same-index band's -- a band 2
	// failure cannot un-pair band 0.)
	if (panel == MascotPanel::Left) {
		if (MascotBand* rightBand = this->rightState.band(bandIndex))
			rightBand->playback().mirrorCopyPending = false;
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
	// Keep drawing what the panel has, and arm a replacement. A panel that is drawing something is
	// advanced by its clip timer or by the beat, so nothing more is needed; a panel that is drawing
	// nothing would have nothing to advance it, so it asks for a replacement itself -- queued, so
	// this never recurses out of a player's signal, and bounded, so a library the backend cannot
	// decode cannot be walked through at load speed.
	// (M3 Task 6A: the rule is per band. The policy band keeps the M2 answer above -- whatever it
	// is drawing is advanced by the beat or its clip timer. A stripe showing a live clip is
	// advanced by its own clip timer, so it is left alone too; a stripe left on a *still* because
	// its trailer failed has no cadence above it, so it asks again rather than staying an image for
	// the session, and its own failure budget stops that ask when the pool cannot serve it.)
	if (!band->media().path.isEmpty()
		&& (bandIndex == 0 || band->media().type == MascotMedia::Video)) {
		return;
	}
	// M3 Task 6A: a right-panel band the left panel mirrors is advanced by the left band's own
	// picks, not by a pick of its own -- drawing a fresh file here would break the pair it exists
	// to keep. A failed copy therefore waits for the next left commit at that same index, which is
	// exactly what the M2 sentence above does for band 0.
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
	// A band past the first that lost a player while showing a video has no cadence of its own
	// yet (Task 6 owns it), so it is re-filled through the image path once this method is done.
	bool refillExtraBands = false;
	// M4 Task 2: the background is in this walk now (it is a per-panel operation, not pair policy),
	// but it is not a pair panel: it never shows images, so losing a player must not queue the image
	// refill or the pair's repaint below. Its bands are the funded prefix, so the band-count clamp
	// has already retired any band the budget stopped carrying.
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		if (!state.surface)
			continue;
		const bool pairPanel = panel != MascotPanel::Background;
		// M3 Task 2: the panel's policy band only. A `Stripes` panel's extra bands get no player in
		// this task -- that is Task 5's per-band decoder budget -- and they show images instead, by
		// design (fillEmptyBands() says so where the fill happens). Once Task 5 generalises this
		// loop over `state.bands` inside the budget, the calls below are already per band.
		// (M3 Task 5: the loop runs per band now, and `bandMayOwnPlayer()` is the budget's answer
		// for each of them. The policy band's downgrade still returns through `downgraded` so
		// applyConfig() repaints the pair; an extra band's is re-filled through the image path,
		// because the cadence that would re-pick a multi-band panel is Task 6's.)
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			const bool mayOwnPlayer = this->bandMayOwnPlayer(panel, i);
			if (mayOwnPlayer && !band->video()) {
				this->createVideoSlot(state, panel, i);
				// M3 Task 6A fix: a band that has just been *given* a player -- a live budget
				// increase, an Images -> Videos apply -- is still showing whatever it showed
				// without one, and nothing else would ask again: `fillEmptyBands()` skips a band
				// that has content, `applyMode()` early-returns, and the old refills fired only on a
				// band that *lost* a player. So the clamp line would claim a player the band never
				// uses. The same queued refill the failure path uses asks for a clip; a band already
				// showing a video is left alone, and the refill itself is mirror-aware, so a
				// mirrored right band waits for the left band's copy instead of drawing its own.
				if (pairPanel && band->media().type != MascotMedia::Video)
					this->queueBandRefill(panel, i, false);
			}
			else if (!mayOwnPlayer && band->video()) {
				// The setting is images-only, or the budget cannot carry a panel: the players go.
				// (M3 Task 6A: the cadence that goes with them is this band's own timer, not the
				// panel's, so a budget change cannot leave a stripe tick over a band it no longer
				// drives.)
				if (QTimer* timer = band->clipTimer())
					timer->stop();
				// A clip that was on screen has no player behind it any more, so `shown` must stop
				// saying it is one. That claim is what onSwapBeat() reads to decide a panel's cadence:
				// a panel still claiming video would be skipped by the beat *and* have no clip timer,
				// i.e. frozen on a frame nothing owns until the app is restarted. The output is left
				// visible, so the QML VideoOutput keeps its last frame on screen until the first image
				// lands -- that commit hides it through applyDisplay() like any other image.
				// (M3 Task 6B: "onSwapBeat()" is onBeat() now, and the cadence no longer tests the
				// type; the claim is still dropped, so a band cannot be advanced as a video that no
				// longer has a player behind it. The paragraph is kept as the record of the reason.)
				// (M3 Task 1: `shown` is the band's `media()`, and forgetMedia() is what drops the
				// claim without touching the slot -- the output keeps its frame for the reason above.)
				const bool hadVideo = band->media().type == MascotMedia::Video;
				if (hadVideo) {
					const QString previousShown = band->media().path;
					band->forgetMedia();
					// The owner field changed, so the pin invariant is re-established the way every
					// other path does it (a video path was never in the image cache, so this is a
					// no-op that keeps the rule uniform).
					this->syncPin(previousShown);
				}
				this->destroyVideoSlot(state, i);
				// (M3 whole-branch fix: a band that already had *no* media when it lost its player
				// is just as contentless -- its first clip may still have been opening, or its fill
				// may have failed -- and it needs the same refill. With `mascots_animated = False`,
				// which is the shipped default, nothing else would ever ask that band again, so it
				// would stay blank for the session. The flag is set for either case.)
				if (hadVideo || band->media().path.isEmpty()) {
					// (M4 Task 2: only a pair panel owes a repaint or a refill. A background band
					// that lost its player is retired by the band-count clamp; handing it an image
					// would put a portrait still behind the window, which is the one thing this
					// layer must never draw.)
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
		// Queued for the same reason applyMode()'s fill is: this runs inside a settings handler and
		// a pick walks the mascot directory. fillEmptyBands() gives the band an image, which is the
		// degradation the budget promises -- never a blank band.
		QMetaObject::invokeMethod(this, [this]() { this->fillEmptyBands(); }, Qt::QueuedConnection);
	}
	return downgraded;
}

void MascotController::createVideoSlot(PanelState& state, MascotPanel panel, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!state.surface || !band || band->video())
		return;
	// A child of this controller, not of the surface: releaseSurface() destroys it *before* the
	// surface, because the sinks it draws into belong to the surface's QML delegate.
	// (M3 Task 1: a child of the band now, which the controller owns through PanelState. The
	// destruction order the sentence above describes is unchanged: destroyVideo() still runs from
	// releaseSurface() before the surface is deleted.)
	// (M3 Task 5: `bandIndex` is the band inside the budget this slot belongs to. Only the policy
	// band's reports are connected below: the per-band cadence that gives the other bands' players
	// work and a commit path is Task 6's, and wiring them to the panel-level handlers now would
	// attribute another band's frame to band 0.)
	// (M3 Task 6A: every band's reports are connected now, each with its own index. The seam the
	// sentence above anticipated is this one: a player on any band reports through the same four
	// signals, and the handler is told which band it belongs to, so a frame from band 2 can never be
	// committed to band 0.)
	band->ensureVideo(0);
	MascotVideoSlot* slot = band->video();
	if (!slot)
		return;
	// The decoder budget and the clip rules are applied here as well as when they change, so a
	// slot created later (an Images -> Videos settings apply) starts from the current
	// configuration instead of from the class defaults.
	slot->setSinglePlayer(!this->videoCrossfades(panel));
	slot->setRandomStart(this->randomStart, this->clipSeconds);
	// The waits come from config, not from the slot's defaults: a slot created after an Apply has to
	// start from the configured values, or a panel enabled later would use a deadline the user had
	// already changed.
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
			// A deadline passed. `kind` is only read when the log words the line, and the ruled
			// rule never drops a file for a timeout, so the shipped value is the honest one.
			this->onVideoFailed(panel, bandIndex, path, MascotVideoSlot::FailKind::Decode, true);
		});
	// A parked clip is not a commit: nothing of it is on screen, so the only thing the controller
	// does with the report is start the mirror pair's copy of the same file at the same offset.
	connect(slot, &MascotVideoSlot::preloaded, this,
		[this, panel, bandIndex](const QString& path, qint64 offsetMs, const QSize& frameSize) {
			this->onVideoPreloaded(panel, bandIndex, path, offsetMs, frameSize);
		});
	// A panel built while the window cannot be seen starts paused, exactly like one that was
	// already running when the window went away.
	// (M3 Task 6A: this is the explicit pause state every newly created player gets. The pause
	// path early-outs when the derived state has not changed, so inheritance is not an option -- a
	// slot created after the last applyVideoPaused() would otherwise run while the window is
	// hidden. It is set on every band's slot here, at the one place a slot is ever built.)
	slot->setPaused(this->videoPaused);
}

void MascotController::destroyVideoSlot(PanelState& state, int bandIndex)
{
	MascotBand* band = state.band(bandIndex);
	if (!band || !band->video())
		return;
	// M3 Task 6A: the cadence goes with the player it advanced, whatever the band index.
	if (QTimer* timer = band->clipTimer())
		timer->stop();
	band->destroyVideo();
	band->setRequestIsVideo(false);
	band->setWantedOffsetMs(-1);
	// The panel-level waits are the policy band's, exactly as the requests they guard are.
	// (M3 Task 6A: the waits are the band's own now, for the same reason the requests are: dropping
	// this band's player drops this band's copy/retry bookkeeping and nothing of another band's.)
	band->playback().mirrorCopyPending = false;
	band->playback().videoPickRetryPending = false;
	band->playback().videoLoadRetries = 0;
}

void MascotController::destroyVideoSlot(PanelState& state)
{
	// M3 Task 2: every band of the panel, not only its policy band. This is the method
	// releaseSurface() and stop() reach for through releaseSurface(), so it is the one place the
	// carried-forward Task 1 finding is closed: no band's players may outlive the surface whose
	// QML delegates own their sinks. A band with no players (the extra bands of a `Stripes` panel
	// in this task) is skipped by the guard inside destroyVideo().
	// Synchronous, like the surface: the players are detached from the sinks they draw into (which
	// still exist at this point) and then deleted.
	// (M3 Task 5: the per-band body is the single-band overload; this loop keeps the teardown of a
	// whole panel in one place, which is what releaseSurface() calls.)
	for (int i = 0; i < static_cast<int>(state.bands.size()); ++i)
		this->destroyVideoSlot(state, i);
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
	// M3 Task 5: a panning mode never crossfades -- it gives every band one player and cuts at the
	// wrap -- so M2's two-players-per-panel verdict is not what its budget means. The budget
	// question there is how many *bands* keep a player, which applyBandBudget() spends and logs;
	// this method only records that no crossfade is possible, so nothing here writes the M2 line,
	// which would describe a fade these modes do not have.
	if (this->columnsLayout.mode != MascotMode::Static) {
		this->videoMode = this->maxDecoders > 0 ? VideoMode::SinglePlayer : VideoMode::Off;
		return;
	}
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

void MascotController::applyBandBudget()
{
	// M3 Task 5: how many bands of each panel keep a player under `mascots_max_decoders`. `Static`
	// is M2's model and this method does not touch it: one band per panel, and whether it gets two
	// players (crossfade) or one (the clamp) is applyDecoderBudget()'s answer. That also means a
	// default `Static` config writes nothing and allocates nothing.
	if (this->columnsLayout.mode == MascotMode::Static) {
		this->leftBandPlayers = 0;
		this->rightBandPlayers = 0;
		// The latch describes a panning allocation. A `Static` interlude has none, so the next
		// panning mode writes its line again instead of comparing against a layout two modes old.
		this->loggedBandBudgetDecoders = -1;
		this->loggedBandBudgetWanted = -1;
		this->loggedBandBudgetAllowed = -1;
		this->loggedBandBudgetLeft = -1;
		this->loggedBandBudgetRight = -1;
		return;
	}
	// Panning modes: one player per band, so the bands are the players that are wanted.
	const int wantedLeft = this->bandCount(MascotPanel::Left);
	const int wantedRight = this->bandCount(MascotPanel::Right);
	const int wanted = wantedLeft + wantedRight;
	int allowedLeft = wantedLeft;
	int allowedRight = wantedRight;
	if (wanted > this->maxDecoders) {
		// Short budget: spend it one player at a time, alternating the panels and taking each
		// panel's bands from index 0. That keeps both panels on video for as long as the budget
		// lasts instead of letting the left panel spend it all, and the left panel takes the odd
		// spare -- it is the band the mirror pair follows, and it is also what a floor split of the
		// budget between two equal panels gives it. The bands past each panel's allocation show
		// images, so a short budget never leaves a band blanker than "no media at all".
		// (M3 Task 5 fix: the order is left then right on every round. Alternating the *starting*
		// panel handed the odd spare to the right -- with 2+2 bands and three decoders the split
		// was left 1 / right 2 -- which contradicted the sentence above. The sentence is the
		// intent; the loop now keeps it.)
		allowedLeft = 0;
		allowedRight = 0;
		int remaining = std::max(0, this->maxDecoders);
		while (remaining > 0) {
			bool took = false;
			for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
				if (remaining <= 0)
					break;
				int& allowed = side == MascotSide::Left ? allowedLeft : allowedRight;
				const int want = side == MascotSide::Left ? wantedLeft : wantedRight;
				if (allowed < want) {
					++allowed;
					--remaining;
					took = true;
				}
			}
			if (!took)
				break;
		}
	}
	// The pair shares one video budget, not two independent ones: `nextImages()` mirrors the left
	// panel's video onto the right and `onSwapBeat()` treats the pair as one video-capable unit, so
	// a panel whose first band has no player can be neither copied into nor advanced and would sit
	// on whatever it had. If the clamp leaves either panel's first band without a player, the pair
	// degrades: neither panel keeps a player and both show images, which is exactly the
	// `VideoMode::Off` behaviour M2 produced for a one-decoder budget and what the beat then drives.
	// A four-way per-panel answer would let a one-video/one-image pair exist, which the mirror rules
	// were never written for.
	// (M3 Task 6B: "onSwapBeat()" is onBeat() now; the pair rule it describes still holds for every
	// band index, and the one-band mode still advances the pair through nextBandImages().)
	bool pairDegraded = false;
	if (allowedLeft == 0 || allowedRight == 0) {
		pairDegraded = allowedLeft > 0 || allowedRight > 0;
		allowedLeft = 0;
		allowedRight = 0;
	}
	this->leftBandPlayers = allowedLeft;
	this->rightBandPlayers = allowedRight;
	// An images-only setting has nothing to clamp and nothing to say: its run's log has to stay
	// what merged master's was, exactly as applyDecoderBudget()'s line does.
	if (!this->mediaSettingWantsVideo())
		return;
	const int allowed = allowedLeft + allowedRight;
	// One line per change of the budget, the bands that ask or the bands that keep a player -- the
	// M2 clamp-line pattern. A count change with the same budget is a different clamp, so it says
	// its piece rather than leaving the last line describing a layout that no longer exists.
	// (M3 Task 5 fix: the per-panel split and the mode join the tuple, because an allocation can
	// change which panels keep the players while leaving the total alone, and a `Scroll`/`Stripes`
	// change is a different sentence with the same counts. The tuple is reset by the `Static`
	// branch above, so a panning mode re-entered after a `Static` interlude says its piece again.)
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
		this->logVideo(QStringLiteral("mascots_max_decoders=%1 cannot give both panels a player (%2 %3 bands ask for one each); neither panel keeps one, so both show images on the beat")
			.arg(this->maxDecoders)
			.arg(wanted)
			.arg(modeLabel(this->columnsLayout.mode)));
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
	this->logVideo(QStringLiteral("mascots_max_decoders=%1 covers %2 %3 bands, one player each (left %4, right %5), hard cut")
		.arg(this->maxDecoders)
		.arg(wanted)
		.arg(modeLabel(this->columnsLayout.mode))
		.arg(allowedLeft)
		.arg(allowedRight));
}

void MascotController::applyBackgroundBudget()
{
	// M4 Task 2: columns first, background last. The pair keeps exactly the entitlement
	// applyDecoderBudget() and applyBandBudget() above gave it -- neither is recomputed here, and
	// neither's line changes -- so the layer is spent what is left of `mascots_max_decoders`.
	// (M4 Task 4: in the panning background modes this task adds, the subtraction is the players
	// the pair actually takes. With `mascots` off or an images-only setting `videoBandsFor()`
	// answers 0 for the pair, so it builds no players at all and reserving its share spent the
	// layer's part of the budget on nobody: with the columns off all of the decoders are the
	// background's, which is what lets a `Stripes` layer draw its configured count of bands. A
	// `Static` background keeps Task 2's subtraction exactly, because its acceptance is
	// byte-identity at every budget; the pair's own allocation and lines are untouched either way.)
	// (M4 review fix: that last sentence became "correct the subtraction in the panning modes only",
	// which asked the wrong question -- the subtraction is a statement about *the pair*, not about the
	// layer, so a pair that builds no players was still charged 2-4 decoders whenever the background
	// was `Static`. A funded layer was then left blank: `mascots` off with
	// `mascots_background_mode=Static` at `mascots_max_decoders=4` asked for 4, subtracted 4 from a
	// pair that had none, and reported "no player left". The question is asked in every background
	// mode now, and a pair that *can* show video is charged exactly what it was charged before, so
	// Task 2's byte-identity case does not move.)
	int pairPlayers = this->leftBandPlayers + this->rightBandPlayers;
	if (this->columnsLayout.mode == MascotMode::Static) {
		// A `Static` pair has no per-band allocation to read: each panel's one band owns two
		// players in `Crossfade` and one in the clamp, and `Off` owns none. That is
		// applyDecoderBudget()'s answer restated as the players it spends.
		const int perPanel = this->videoMode == VideoMode::Crossfade ? PLAYERS_PER_PANEL
			: (this->videoMode == VideoMode::SinglePlayer ? 1 : 0);
		pairPlayers = perPanel * VIDEO_PANEL_COUNT;
	}
	if (!this->mediaMayBeVideo(MascotPanel::Left) && !this->mediaMayBeVideo(MascotPanel::Right))
		pairPlayers = 0;
	if (!this->backgroundEnabled) {
		// A layer that is off owns nothing and says nothing. The latches are reset with it, so a
		// later enable states its allocation instead of comparing against a previous episode.
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
		// One band, and it may own two players -- one fades out while the other fades in -- so the
		// spend is what the crossfade design asks for, clamped by what is left.
		players = std::min(remaining, PLAYERS_PER_PANEL);
	}
	else {
		// `Scroll` and `Stripes` give every band one player, so the spend is one per band the mode
		// has: one band for `Scroll`, `mascots_background_stripes` for `Stripes`. The clamp by
		// `remaining` is what makes the band count a funded prefix rather than a wish.
		// (M4 Task 6: `Scroll` is the marquee now and spends `mascots_background_stripes` as well --
		// both panning modes use the same "how many clips are in play" key, and the funded prefix
		// is still the clamp.)
		const int wantedBands = this->backgroundStripes;
		players = std::min(remaining, wantedBands);
	}
	this->backgroundPlayers = players;
	this->backgroundCrossfades = mode == MascotMode::Static && players == PLAYERS_PER_PANEL;
	const int bands = this->backgroundBandCount();
	// One line per change of the tuple -- the budget, the players and the bands they bought -- for
	// the reason the M2 clamp line has one: the allocation is a statement about the configuration,
	// not an event, so repeating it on every Apply would only bury the rest of the log.
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
	// One timer per panel, owned by the controller so it survives the enable/disable cycles the
	// surfaces do not, and single-shot because every tick is answered by a fresh commit that
	// re-arms it.
	// (M3 Task 6A: one timer per band, owned by the band, for the same two reasons and one more --
	// it dies with the band it advanced, so no cadence can tick for a band the panel no longer
	// has. The controller still connects its timeout, because the band has no controller to tell.)
	if (!band->clipTimer()) {
		QTimer* timer = band->ensureClipTimer();
		connect(timer, &QTimer::timeout, this, [this, panel, bandIndex]() {
			this->onClipTimer(panel, bandIndex);
		});
	}
	QTimer* timer = band->clipTimer();
	// 0 disables the timer entirely: the clip loops and the panel advances only on the beat.
	if (this->clipSeconds <= 0 || this->videoPaused) {
		timer->stop();
		return;
	}
	// Under the mirror key the pair shows one media, so one timer advances it: the left panel's.
	// Two timers armed a few milliseconds apart would each ask for a swap and the second would
	// supersede the first -- two loads per clip.
	// (M3 Task 6A: "the left panel's" is the left panel's *same-index band's* now. A right-panel
	// band numbered past the left panel's count is not mirrored, so it is advanced by its own
	// timer and this rule leaves it running.)
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex)) {
		timer->stop();
		return;
	}
	timer->start(std::min(this->clipSeconds * 1000, MAX_CLIP_TIMER_MS));
}

void MascotController::onClipTimer(MascotPanel panel, int bandIndex)
{
	if (!this->running)
		return;
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!band || !band->video() || band->media().type != MascotMedia::Video)
		return;
	// The panel that timed out advances. Under the mirror key the pair advances together, which
	// only the left panel's timer ever asks for (restartClipTimer keeps the right one stopped).
	// (M3 Task 6A: "the panel" is the band that owns the timer, which is what keeps a stripe's
	// cadence off band 0; "the pair" is the same band index on both panels. Four stripes with
	// `mascots_clip_seconds > 0` therefore run four independent cadences.)
	if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
		this->nextBandImages(bandIndex, false);
		return;
	}
	this->pickFor(state, panel, bandIndex, bandIndex == 0);
}

void MascotController::stopClipTimers()
{
	// M4 Task 2: every panel's bands, the background's included -- a timer is a per-band cadence,
	// and one that survived a disable would advance a layer that is no longer there.
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
	// M4 Task 2: the background's bands are in this walk now, because the pause rule is a per-panel
	// one -- a layer whose columns are off must still release its decoders when the window cannot
	// be seen. Its own clips resume and re-arm exactly like a column's.
	for (MascotPanel panel : { MascotPanel::Left, MascotPanel::Right, MascotPanel::Background }) {
		PanelState& state = this->stateFor(panel);
		// M3 Task 5: the pause rule reaches every band's players, not only the policy band's. A
		// band given a player by the decoder budget is playing (or parked) just as the policy band
		// is, and a paused panel must hold no decoder on any of them. `Static` has one band and
		// makes exactly the calls it made before.
		// (M3 Task 6A: one loop for every band, the policy band's first. The blocker below that
		// handled an extra band's player and the policy band's separately is gone with the
		// policy-band-only timer: each band's own cadence and its own slot are answered together.)
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			MascotVideoSlot* slot = band ? band->video() : nullptr;
			if (!slot)
				continue;
			slot->setPaused(pause);
			if (pause) {
				// A panel that is being paused still has to give up a parked clip: the decoder it holds
				// is open for a panel nobody can see, which is the cost this rule removes. The resume
				// branch below arms a fresh one, so nothing is lost but the lead time.
				// (M3 Task 6A: the merged loop below is the `if (!state.clipTimer)` branch and the
				// timer branch together -- every band has a player here, each with its own timer, so
				// the two cases are one.)
				// Nothing may advance while nothing can be seen: a timer that fired here would start a
				// load whose first frame is what the gate reads as "a swap happened".
				// (M3 Task 6A: the timer is this band's, and a paused panel stops every one of them;
				// a single shared timer used to be all a panel had.)
				if (QTimer* timer = band->clipTimer())
					timer->stop();
				// And the parked clip goes with the cadence: it is the next swap made ready, and a paused
				// panel has no next swap to make it ready for. Kept, it would hold a decoder open on a
				// panel nobody can see -- exactly what the pause rule exists to stop.
				// (M4 Task 2: the background never preloads, so a parked clip is only ever a column's --
				// discarding it here is a no-op for the layer, and the call stays for the reason above.)
				slot->discardPreload();
			}
			else if (band->media().type == MascotMedia::Video && !band->media().path.isEmpty()) {
				// The clip on screen is resumed by the player itself; its cadence starts again from here.
				// (M3 Task 6A: this band's own cadence, on this band's own timer.)
				this->restartClipTimer(panel, i);
				// And the next clip is armed here rather than left to the next commit: the preload that
				// would have been parked was refused while the panel was hidden (a parked decoder is
				// exactly the cost the pause rule removes), so this is where the panel catches up -- one
				// clip of lead time before the timer that was just restarted asks for it.
				// (M3 Task 6A: only the policy band preloads; startPreload() itself only acts in
				// `Static`, where a second player exists to park a clip on.)
				// (M4 Task 2: and the background never preloads at all -- startPreload() refuses it --
				// so the layer's resume is the cadence above and nothing else.)
				if (i == 0 && panel != MascotPanel::Background)
					this->preloadNext(panel);
			}
		}
	}
	// Debug-gated like every line below it, and silent for an images-only setting: a pause that no
	// player observes is not something the gate should read, and the images run's log has to stay
	// what merged master's was.
	// (M4 Task 2: unless the background is drawing -- it is video-only, so its players observe the
	// pause whatever `mascots_media` says about the columns.)
	if (this->mediaSettingWantsVideo() || this->backgroundBandCount() > 0) {
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

void MascotController::logScroll(const QString& message) const
{
	// Debug-gated exactly like logVideo(), and for the same reason: the Logger writes to the log
	// window always and to OutputDebugString only in debug mode, so gating here means an ordinary
	// run writes nothing while the gate (which runs with -debug) reads every line. A tag of its own
	// -- MASCOT-SCROLL -- so the scroll lines can be counted apart from the video ones, and so a
	// `Static` run's log is byte-identical to merged master's.
	if (!this->App || !this->App->debug_mode || !this->App->logger)
		return;
	this->App->logger->log(SCROLL_LOG_TAG + message, QStringLiteral("MASCOT-SCROLL"));
}

void MascotController::logBackground(const QString& message) const
{
	// M4 Task 2: debug-gated exactly like logVideo()/logScroll(), and for the same reason: the
	// Logger writes to the log window always and to OutputDebugString only in debug mode, so gating
	// here means an ordinary run writes nothing while the gate (which runs with -debug) reads every
	// line. A tag of its own -- MASCOT-BG -- so the layer's allocation and gating can be counted
	// apart from the columns' video lines, and so a run with `mascots_background` off writes none.
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
	// Amendment W: the controller takes the beat connections itself. MainApp's old handlers
	// were gated on the retired widgets' isVisible(), and those widgets were hidden after the
	// cutover, so a branch inside them could never run.
	// (M3 Task 6B: one connection, to the pulse, so one beat is handled once. The two legacy
	// signals stay on the thread for any other listener but are deliberately not connected here:
	// connecting them as well as the pulse would handle every beat twice.)
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
	// M2: a video panel's cadence is its clip timer, not the beat (§7.9's "the clip timer governs
	// video slots"). A beat arrives every `mascots_frequency` milliseconds, which is far shorter
	// than any clip length, so letting it advance a video panel would churn clips several times a
	// second and the clip timer would never be the thing that decided anything. The test is what a
	// panel is *showing*, not what the setting asks for: a panel that fell back to images is
	// cadenced like an image panel, so a library of unusable files cannot freeze it, and
	// `mascots_media=Images` keeps M1's cadence exactly because nothing there is ever a video.
	// (M3 Task 6B: the paragraph above is kept as the record of the two-signal cadence. There is
	// one cadence now, and the clip length gates it instead of excluding video: a *video* band
	// advances only once its content has been up for half a clip, and a beat that finds it too
	// young flips instead. An image band has no clip, so it is never floored -- its rate limit is
	// the dice, which keeps a default `Images` run's cadence exactly as it was. The clip timer is
	// untouched -- it still runs and still advances a video band at the full clip length.)
	//
	// `mascots_clip_seconds` = 0 is the one setting under which the beat *is* the video cadence
	// (the clip loops and only the beat moves it on), so the beat keeps working for a video panel
	// exactly as it did before this branch existed.
	// (M3 Task 6B: with no clip length there is no floor either, so every swap beat advances --
	// the same "the beat is the cadence" the sentence above describes.)
	//
	// The pair is one media, so it is advanced by one thing: its clip timer while that timer is
	// running, and the beat only when there is no timer at all.
	// (M3 Task 6B: the beat also advances a video band once half a clip has passed; the sentence
	// above is kept as the record of the two-signal cadence the floor replaces.)
	if (!this->running || !this->beatsAllowed())
		return;
	// M4 Task 2: the background's cadence rides the swap beat, and only it -- the layer has no flip
	// state and no pair, so on a non-swap beat (or with `mascots_swap_on_beat` off) it does nothing
	// at all: it is not flipped with the pair below and does not add to it. The block sits *here*,
	// immediately after the visibility guard, because everything from the next `if` on is the pair's
	// flip path and a background case placed after it would never run. Each band that the half-clip
	// floor accepts is advanced through the same scheduleBeatAdvance()/commitBeatAdvance() pair a
	// column band uses, so a panning background later defers to its own seam for free.
	if (swapWanted && this->swapOnBeat && this->backgroundState.surface) {
		for (int i = 0; i < static_cast<int>(this->backgroundState.bands.size()); ++i) {
			MascotBand* band = this->backgroundState.band(i);
			if (!band || !this->bandMayAdvance(*band))
				continue;
			this->scheduleBeatAdvance(MascotPanel::Background, i);
		}
	}
	// A non-swap beat, or `mascots_swap_on_beat = False`, is the retired flip signal's whole
	// action: both panels flip, and no band advances on this beat.
	if (!swapWanted || !this->swapOnBeat) {
		// M4 Task 2: the flip is the pair's, so it happens only while the columns are on. A
		// background-only run keeps the beat alive for the layer above and has no pair to flip.
		if (this->columnsEnabled)
			this->flipBoth();
		return;
	}
	// M4 Task 2: with the columns off the pair has no cadence at all. The background block above
	// has already taken its advance; returning here stops the pair's own swap/flip path from
	// decoding and writing for two hidden hosts, which is the same "do neither" the seed path keeps
	// on start().
	if (!this->columnsEnabled)
		return;
	if (this->columnsLayout.mode == MascotMode::Static) {
		// M1, untouched: the pair advances together and the mirror key decides whether the right
		// panel copies the left one.
		// (M3 Task 6B: one band index at a time now, and a one-band mode advances the pair through
		// nextBandImages() -- the body nextImages() had. A `Stripes` panel walks every index
		// instead, and a panning mode queues each advance on its own band.)
		// (M3 Task 6B fix: under the mirror key the pair is one unit, so it advances only when the
		// *oldest* of its two commits meets the floor -- the right band's copy lands a little after
		// the left's, and advancing one side alone would leave the pair showing different files.
		// With the key off, each side is gated by its own content age.)
		MascotBand* leftBand = this->leftState.band(0);
		MascotBand* rightBand = this->rightState.band(0);
		const bool leftMay = leftBand && this->bandMayAdvance(*leftBand);
		const bool rightMay = rightBand && this->bandMayAdvance(*rightBand);
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
	// Panning modes: every band of both panels is asked, and each eligible band's advance is
	// queued on that band (or committed now when it has no seam). Under the mirror key a pair
	// index is one unit and advances only when both sides meet the floor; a right band past the
	// left panel's count has no counterpart, so it advances on its own.
	bool advanced = false;
	const int bandTotal = std::max(
		static_cast<int>(this->leftState.bands.size()),
		static_cast<int>(this->rightState.bands.size()));
	for (int i = 0; i < bandTotal; ++i) {
		if (this->bandMirrorsLeft(i)) {
			// (M3 Task 6B fix: the pair waits for the older of its two commits, and a beat that
			// finds it younger flips rather than advancing one panel alone.)
			MascotBand* leftBand = this->leftState.band(i);
			MascotBand* rightBand = this->rightState.band(i);
			if (!this->leftState.surface || !this->rightState.surface || !leftBand || !rightBand
				|| !this->bandMayAdvance(*leftBand) || !this->bandMayAdvance(*rightBand))
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
			if (!this->bandMayAdvance(*band))
				continue;
			advanced = true;
			this->scheduleBeatAdvance(panelOf(side), i);
		}
	}
	// Nothing could advance, so the beat is a flip rather than nothing at all.
	if (!advanced)
		this->flipBoth();
}

bool MascotController::bandMayAdvance(const MascotBand& band) const
{
	// (M3 Task 6B: the half-clip floor. `mascots_clip_seconds` <= 0 disables it, and a band that
	// has never committed anything has no age to measure, so both are "may advance". The clock is
	// read here, at beat time, because a queued advance is re-evaluated by its seam anyway.)
	// (M3 Task 6B fix: the floor is a *clip* floor, so it applies only to a band that is showing a
	// video. An image band has no clip of its own; its rate limit is the existing dice
	// (`mascots_random_change`/`mascots_random_chance`), which keeps a default `Images`
	// configuration byte-identical to def041d.)
	// (M4 Task 2: `mascots_clip_seconds` stays shared between the columns and the background on
	// purpose -- there is one cadence clock, and the clip length is not part of the background's
	// layout. Do not give the layer a clip length of its own: two clocks would let the pair and the
	// layer drift apart, and nothing asks for that.)
	if (band.media().type != MascotMedia::Video)
		return true;
	if (this->clipSeconds <= 0)
		return true;
	const qint64 committedAt = band.playback().committedAtMs;
	if (committedAt <= 0)
		return true;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	return now - committedAt >= static_cast<qint64>(this->clipSeconds) * 500;
}

bool MascotController::bandReachesSeam(MascotPanel panel, const MascotBand& band) const
{
	// (M3 Task 6B: whether a pass is actually running for this band. A queued wrap commit has no
	// seam when the driver is stopped or would take a still branch, so onBeat() must commit now
	// rather than wait for a trigger that never comes. Every term below is one of the reasons
	// onScrollTick()'s band loop would not reach a seam.)
	const PanelLayout& layout = this->layoutFor(panel);
	if (!this->running || layout.mode == MascotMode::Static)
		return false;
	if (!this->scrollTimer || !this->scrollTimer->isActive())
		return false;
	// Hidden, minimized or paused by the window rule: no tick will paint, and updateScrollDriver()
	// stops the timer on these. Asked here as well so a missed event cannot strand a queued change.
	if (!this->scrollCanTick())
		return false;
	if (layout.scrollSpeed <= 0)
		return false;
	// (M4 Task 6: the marquee always reaches a wrap -- every cell crosses a multiple of `total`
	// within one cycle -- so a queued advance may defer to it whenever the driver is running and
	// the axis has a length. The travel test below is the content-pan question and does not apply:
	// a marquee cell's own content does not move inside it any more. A background `Scroll` with
	// only one funded band is the static-cell case, so it has no seam at all and the caller commits
	// now rather than queueing a change no tick would ever run.)
	if (panel == MascotPanel::Background && layout.mode == MascotMode::Scroll) {
		if (!this->backgroundMarqueeActive(panel))
			return false;
		const PanelState& marqueeOwner = this->stateFor(panel);
		if (!marqueeOwner.surface)
			return false;
		const QRectF frame(marqueeOwner.surface->rect());
		const double axisLen = this->scrollPanIsVertical(panel) ? frame.height() : frame.width();
		return axisLen > 0.0;
	}
	const double travel = this->scrollTravel(panel, band);
	if (travel <= 0.0)
		return false;
	// The tick measures its phase advance against the panel's widest band. A band whose own share
	// of that advance is a whole travel or more is the sub-tick still branch, and it has no seam.
	const PanelState& owner = this->stateFor(panel);
	if (owner.band(band.slotIndex()) != &band)
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
	PanelState& state = this->stateFor(panel);
	MascotBand* band = state.band(bandIndex);
	if (!band)
		return;
	if (this->bandReachesSeam(panel, *band)) {
		// (M3 Task 6B: the change is deferred to this band's next wrap point. The closure names
		// only the side and the index and resolves the band fresh when the seam runs, so a band
		// destroyed between the beat and the seam cannot be written through a stale pointer.)
		band->queueWrapCommit([this, panel, bandIndex]() {
			this->commitBeatAdvance(panel, bandIndex);
		});
		return;
	}
	// (M3 Task 6B: no pass is running, so there is no seam to defer to and the change is applied
	// now. Waiting would let a stopped driver hold the swap forever.)
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
	// (M3 Task 6B: a mirrored right band is advanced by the left band's commit, which carries the
	// pair, so committing it here as well would replace the same media twice. Everything below is
	// content work only: no applyConfig() and no configureSurface() is reachable from a beat.)
	if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
		return;
	// (M3 whole-branch fix: this commit may be running at a seam a whole clip after the beat that
	// queued it, so the conditions that allowed it are asked again. Without the re-ask, a
	// `mascots_swap_on_beat = False` applied in between would still advance the band once, and
	// content committed in between would be replaced inside the half-clip floor -- a swap that the
	// current configuration does not permit, from a beat that no longer reflects it.)
	if (!this->swapOnBeat || !this->bandMayAdvance(*band))
		return;
	if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
		MascotBand* rightBand = this->rightState.band(bandIndex);
		if (!rightBand || !this->bandMayAdvance(*rightBand))
			return;
		this->nextBandImages(bandIndex, false);
		return;
	}
	this->pickFor(state, panel, bandIndex, bandIndex == 0);
}

bool MascotController::beatsAllowed() const
{
	// MainApp's handlers used to test the old image widget's `isVisible()` together with
	// utils::IsMyWindowVisible(); the surface is that widget's replacement, so the same two
	// conditions apply to it.
	// (M3 Task 4: the two conditions are panelsVisible() now. The scroll driver asks the same "can
	// a user see the panels" question and must not grow a second copy of it, so the test moved
	// into that function and this one delegates -- the answer is unchanged.)
	return this->panelsVisible();
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
		PanelState& state = this->stateFor(panelOf(side));
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
			// (M3 Task 6A: every band arms its own speculation now, so `armCandidate` is true for
			// every band; `showMedia()` arms the band it committed.)
			this->showMedia(state, panelOf(side), media, false, true, i);
		}
		// The speculative half stays with band 0: `prefetchPath` and the candidate are one deep
		// *per panel* until Task 5/6 give the bands their own (M3 Task 2). A policy band that is
		// waiting for a video keeps its wait, exactly as it did before bands were counted.
		// (M3 Task 6A: one deep *per band* now, so every band the delivery belongs to is asked,
		// not only band 0. A band waiting for a video keeps its wait for the same reason.)
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
	// Nothing arrives for this path, so nothing is applied: the panel keeps what it has and the
	// next swap re-rolls. Clearing both waits is what keeps a later successful delivery of the
	// same path from landing on a panel that has moved on, and what lets the next commit arm a
	// fresh candidate instead of waiting for one that will never come.
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		PanelState& state = this->stateFor(panelOf(side));
		// M3 Task 2: every band, so an extra band whose fill failed is not left holding a request
		// that will never be satisfied (fillEmptyBands() reads that request to decide whether the
		// band already has content coming). A failed fill is not retried here: the panel's next
		// swap re-rolls band 0, and an extra band is re-filled by the next layout change -- the
		// per-band cadence that would retry it is Task 6's.
		// (M3 Task 6A: a failed fill *is* retried here for a band past the first. Such a band has
		// no beat above it, so without this ask one undecodable file would leave a stripe blank
		// for the session -- the hole the Task 2 review named. The ask is queued and bounded by the
		// band's own failure budget, which a commit resets, so a pool the backend cannot decode is
		// not walked at decode speed.)
		for (int i = 0; i < static_cast<int>(state.bands.size()); ++i) {
			MascotBand* band = state.band(i);
			if (!band)
				continue;
			// M2: an image decode failing says nothing about the video a panel may be loading, so a
			// panel waiting for a player's frame keeps its wait.
			// (M3 Task 2: "a panel" is any band of it -- an extra band waiting for a video cannot
			// happen in this task, but the rule is per band either way.)
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
		// (M3 Task 6A: the speculative path is the band's, so every band's is cleared.)
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
	// M4 Task 2: the background refuses a refill outright. A refill's answer is an image -- the one
	// thing the layer must never show -- so a background band that lost its player (or its clip)
	// gets no replacement here: the budget's answer is the band-count clamp in bandCount(), which
	// syncBands() applies by retiring the band, and a band that keeps its player is left to its own
	// pick path (the beat or its clip timer).
	if (panel == MascotPanel::Background)
		return;
	// Queued on `this`, for the reason onVideoFailed()'s older ask was: this runs out of a decode
	// or player signal, and the refill draws from the pool and posts work. A controller stopped in
	// between simply never runs it.
	QMetaObject::invokeMethod(this, [this, panel, bandIndex, onlyIfEmpty]() {
		if (!this->running)
			return;
		PanelState& panelState = this->stateFor(panel);
		MascotBand* band = panelState.band(bandIndex);
		if (!panelState.surface || !band)
			return;
		// The failure this ask answers may have been superseded by a commit in the same turn. A
		// blank band is the case `onlyIfEmpty` names; otherwise a band that is now showing a live
		// clip must keep it -- its own clip timer advances it -- while a band still on a still is
		// the one that asked for another try.
		if (onlyIfEmpty) {
			if (!band->media().path.isEmpty())
				return;
		}
		else if (band->media().type == MascotMedia::Video && !band->media().path.isEmpty()) {
			return;
		}
		// M3 Task 6A fix: under the mirror key a band index is one media on both panels, so the
		// pair is advanced from the left panel's band. A mirrored right band must not draw a file
		// of its own here -- that would unpair it -- and a left band that is mirrored advances the
		// pair through nextBandImages(), whose copy is what the right band receives. A right-panel
		// band past the left panel's count, and every band when the key is off, picks on its own.
		if (panel == MascotPanel::Right && this->bandMirrorsLeft(bandIndex))
			return;
		if (panel == MascotPanel::Left && this->bandMirrorsLeft(bandIndex)) {
			this->nextBandImages(bandIndex, false);
			return;
		}
		this->pickFor(panelState, panel, bandIndex, bandIndex == 0);
	}, Qt::QueuedConnection);
}
