#include "stdafx.h"
#include "MascotController.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "VideosModel.h"
#include "utils.h"
#include <QDateTime>
#include <QEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

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
	constexpr int VIDEO_SINK_RETRY_LIMIT = 5;
	constexpr int VIDEO_SINK_RETRY_MS = 150;

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
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (state->video)
			state->video->shutdown();
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

	const bool mirror = this->mirrorEnabled();
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		// The surface-wide default. The per-slot value written by applyDisplay() below is
		// what is drawn, so the default can never disagree with the picture.
		state->surface->setMirror(mirror);
		// Applying config drops a user flip, which is what the retired renderer did when the
		// settings were applied: the side rule becomes authoritative and the repaint below draws
		// what the settings say, never a leftover toggle.
		state->flipToggled = false;
	}
	// A mode change repaints what is already on screen; the surface default alone would only
	// affect a slot created later.
	this->applyDisplay(this->leftState, MascotSide::Left);
	this->applyDisplay(this->rightState, MascotSide::Right);
	this->updateLayout();
	// The player half follows the new media setting and the new budget: an `Images` setting, or a
	// budget below what a panel needs, gives the panel back its memory instead of leaving idle
	// players behind, and a setting that becomes video-capable builds them.
	this->ensureVideoSlots();
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
		if (state.shown.type == MascotMedia::Video && !state.shown.path.isEmpty())
			this->restartClipTimer(state, side);
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

void MascotController::updateLayout()
{
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		// The slot fills the surface that hosts it, and the surface derives the content shift
		// from this rectangle itself (amendment R: no centring arithmetic here).
		state->surface->setSlotRect(0, QRectF(state->surface->rect()));
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
		if (this->leftState.requestedIsVideo) {
			MascotVideoSlot* leftVideo = this->leftState.video.data();
			if (leftVideo && leftVideo->isBusy()) {
				// Still in flight: the right panel's copy is started from the left panel's
				// firstFrame(), which carries the offset the pair has to agree on.
				this->preparePick(this->rightState, false);
				this->rightState.mirrorCopyPending = true;
				return;
			}
			if (this->leftState.shown.type == MascotMedia::Video && !this->leftState.shown.path.isEmpty()) {
				// Already showing: the offset is committed, so the copy needs no report.
				this->startMirrorVideo(this->leftState.shown.path, this->leftState.shown.startOffsetMs);
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
			this->showMedia(this->rightState, MascotSide::Right, left, false, true);
			return;
		}
		// The left panel's pick is still in flight (H1): copy the path it is about to show, not
		// the one it still shows, and the single delivery fills both panels.
		if (this->leftState.requestedPath.isEmpty())
			return;
		this->preparePick(this->rightState, false);
		this->requestPath(this->rightState, this->leftState.requestedPath);
	}
	else {
		this->pickFor(this->rightState, MascotSide::Right, false);
	}
}

void MascotController::flipSide(MascotSide side)
{
	SideState& state = this->stateFor(side);
	// The retired renderer's flip was a no-op on an empty pixmap, so flipping a panel that shows
	// nothing is not a state change here either.
	if (!state.surface || state.shown.path.isEmpty())
		return;
	state.flipToggled = !state.flipToggled;
	this->applyMirror(state, side);
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
	// The retired renderer's getColor() semantics, unchanged, on the palette the decode already
	// extracted (amendment H5): weight by area when asked, otherwise take the dominant area
	// out of both lists. An invalid QColor means "no palette", which the caller ignores.
	color_area color;
	if (weighted) {
		if (!state.shown.acceptedColors.isEmpty()) {
			QList<color_area> accepted = state.shown.acceptedColors;
			color = utils::get_weighted_random_color(accepted);
		}
		else if (!state.shown.rejectedColors.isEmpty()) {
			QList<color_area> rejected = state.shown.rejectedColors;
			color = *utils::select_randomly(rejected.begin(), rejected.end());
		}
	}
	else {
		for (const color_area& candidate : state.shown.acceptedColors) {
			if (candidate.area_percent > color.area_percent)
				color = candidate;
		}
		for (const color_area& candidate : state.shown.rejectedColors) {
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
	this->requestPath(state, path);

	if (!this->mirrorEnabled())
		return;
	SideState& otherState = this->stateFor(other(side));
	if (!otherState.surface)
		return;
	otherState.mirrorOverrideEnabled = true;
	otherState.mirrorOverrideValue = true;
	otherState.flipToggled = false;
	otherState.highlightOnApply = false;
	this->requestPath(otherState, path);
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
	surface->setSlotCount(1);
	// The image path's mode (M1): the media is scaled to the panel's height with its aspect
	// ratio kept, which is what the old renderer's scaled(width, height, KeepAspectRatio)
	// produced -- a tall panel keeps a narrow image's side margins and crops a wide one
	// horizontally. The surface owns that geometry (amendment R); the controller only picks the
	// mode. A later milestone that shows video picks its own mode here.
	// M2: that milestone is this one, so the write moved to applyDisplay(), which picks the mode
	// per media type -- and both types pick HeightFit (§8 M2 ruling), so what is drawn is the
	// mode this comment describes. The comment stays as the record of why.
	surface->setMirror(this->mirrorEnabled());
	this->applyMirror(state, side);
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
	this->ensureVideoSlots();
}

void MascotController::releaseSurface(SideState& state)
{
	if (!state.surface)
		return;
	state.surface->removeEventFilter(this);
	// M2: the players go before the surface. They draw into QVideoSinks that belong to the
	// surface's QML delegate, so destroying the surface first would leave them holding pointers
	// into objects that no longer exist. shutdown() is the synchronous detach Task 2 asks for.
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
	const QString previousShown = state.shown.path;
	const QString previousCandidate = state.candidate.path;
	// M2: a cleared panel must not keep a clip playing into a slot nobody draws, and `clearSlot()`
	// deliberately leaves the video state alone (Task 1's interface fact), so both halves are done
	// here: the players are stopped and their outputs hidden.
	if (state.clipTimer)
		state.clipTimer->stop();
	if (state.video)
		state.video->stopAll();
	if (state.surface && state.videoOutputVisible)
		state.surface->setSlotVideoVisible(0, false);
	state.videoOutputVisible = false;
	state.requestedIsVideo = false;
	state.mirrorCopyPending = false;
	state.wantedOffsetMs = -1;
	state.videoFailures = 0;
	if (state.surface)
		state.surface->clearSlot(0);
	state.requestedPath.clear();
	state.prefetchPath.clear();
	state.shown = MascotMedia();
	state.candidate = MascotMedia();
	state.flipToggled = false;
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.highlightOnApply = false;
	// Amendment H2: the owner fields are gone before the pins are re-evaluated, so a path that
	// only this panel referenced is unpinned and one another panel still references is not.
	// A pinned entry is never evicted, which is why a missed unpin is invisible until the
	// cache has grown past every bound.
	this->syncPin(previousShown);
	this->syncPin(previousCandidate);
}

bool MascotController::pinReferenced(const QString& path) const
{
	if (path.isEmpty())
		return false;
	// Every field that keeps a media alive on screen or in reserve. A cache entry is pinned
	// exactly while one of them still names its path.
	return this->leftState.shown.path == path || this->leftState.candidate.path == path
		|| this->rightState.shown.path == path || this->rightState.candidate.path == path;
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
	if (!state.surface || state.shown.path.isEmpty())
		return;
	this->applyMirror(state, side);
	// M2: the fill mode is written here rather than once per surface, because it now follows the
	// media type. Both types ask for HeightFit (§8's ruling: a video resizes vertically to fit and
	// centres horizontally, exactly like an image), so in practice the value only differs from
	// M1's when a later milestone adds `mascots_fill` -- but the decision belongs next to the
	// media it describes, and the `slotFillModeKnown` test keeps the image path from writing a
	// property it already carries.
	const MascotSurface::FillMode fillMode = MascotSurface::FillMode::HeightFit;
	if (!state.slotFillModeKnown || state.slotFillMode != fillMode) {
		state.surface->setSlotFillMode(0, fillMode);
		state.slotFillMode = fillMode;
		state.slotFillModeKnown = true;
	}
	if (state.shown.type == MascotMedia::Video) {
		state.surface->setSlotMedia(0, state.shown);
		// A video's content shift is 0: there is no alpha silhouette to centre (the media's own
		// frame is the content), and the slot is already the frame's height-normalised rectangle.
		state.surface->setSlotContentOffset(0, QPointF(0.0, 0.0));
		// Shown, not faded in: the opacity stays where fadeInVideo() left it, which is 0 for a
		// fresh panel, so the output is receiving frames (and the gate can see the first one)
		// while nothing of it is visible yet (M2 ruling: show the output before asking for a clip).
		state.surface->setSlotVideoVisible(0, true);
		state.videoOutputVisible = true;
		return;
	}
	state.surface->setSlotMedia(0, state.shown);
	// The panel is back on the image branch: the video outputs go away and the players stop, so a
	// dropped image on a video panel costs a decoder for exactly as long as the drop takes. The
	// flag is what keeps an images-only configuration from writing any of this at all.
	if (state.videoOutputVisible) {
		state.surface->setSlotVideoVisible(0, false);
		state.videoOutputVisible = false;
		if (state.video)
			state.video->stopAll();
	}
	// An image panel has no cadence of its own: M1's beat-driven behaviour is the whole of it,
	// and a clip timer left running would advance a panel the beat is already advancing.
	if (state.clipTimer)
		state.clipTimer->stop();
	// `mascots_center_content` off means "centre the whole image, not its content", which is
	// the surface's own shift set to zero. The media write above resets any previous
	// override, so this has to come after it (amendment R: the surface owns the arithmetic,
	// this only says whether to use it).
	if (!this->centerContentEnabled())
		state.surface->setSlotContentOffset(0, QPointF(0.0, 0.0));
}

void MascotController::applyMirror(SideState& state, MascotSide side)
{
	if (!state.surface)
		return;
	bool mirrored;
	if (state.mirrorOverrideEnabled)
		mirrored = state.mirrorOverrideValue;
	else if (!state.shown.path.isEmpty())
		mirrored = this->flipForSide(state.shown.path, side);
	else
		mirrored = false;
	// The retired renderer toggled whatever the side rule had produced, and a new image reset that
	// toggle. One authoritative value per slot, written here and nowhere else.
	if (state.flipToggled)
		mirrored = !mirrored;
	state.surface->setSlotMirrored(0, mirrored);
}

void MascotController::requestPath(SideState& state, const QString& path)
{
	state.requestedPath = path;
	// M2: this is the image branch's request, so a video the panel was still loading is no longer
	// what it is waiting for -- its first frame will be ignored rather than applied over the
	// image (a dropped image on a video panel is exactly this case).
	state.requestedIsVideo = false;
	state.mirrorCopyPending = false;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

MascotMedia MascotController::pickFor(SideState& state, MascotSide side, bool updateHighlight)
{
	if (!this->running || !state.surface)
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
		this->showMedia(state, side, media, true, true);
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
	if (!state.requestedPath.isEmpty()) {
		MascotPrefetcher* source = this->prefetcher.data();
		if (!source || source->isPending(state.requestedPath))
			return MascotMedia();
	}

	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return MascotMedia();

	this->preparePick(state, updateHighlight);
	this->requestPath(state, path);
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
	bool fromReady, bool armCandidate)
{
	if (!state.surface || media.path.isEmpty())
		return;

	// Amendment H2: the owner fields change first and the pins are re-evaluated afterwards, so a
	// path that stays referenced -- the candidate becoming the displayed media, or the other
	// panel showing the same file -- is never unpinned even for one statement, which would let
	// eviction take the pixels between the two.
	const QString previousShown = state.shown.path;
	const QString previousCandidate = state.candidate.path;
	if (fromReady)
		state.candidate = MascotMedia(); // consumed by this commit
	state.shown = media;
	state.flipToggled = false;
	// A commit supersedes whatever this panel was still waiting for: a delivery for an older pick
	// must not overwrite what is on screen a beat later (Important #1). This is the one place it
	// is dropped, so every commit path -- a candidate, the mirror copy, an awaited delivery --
	// drops it, and the panel is armed for a fresh candidate below instead.
	state.requestedPath.clear();
	// M2: the wait this commit supersedes is the image one; a video wait cannot survive a commit
	// either, and clearing the flag is what makes a late first frame of an older clip ignorable.
	state.requestedIsVideo = false;
	state.prefetchPath.clear();
	// The media that just became visible is pinned *before* the two paths it replaced are
	// re-evaluated: each syncPin() can run eviction, and at the configured floor the entry eviction
	// would then be free to take is exactly this one (M1).
	this->syncPin(media.path);
	this->applyDisplay(state, side);
	this->syncPin(previousShown);
	this->syncPin(previousCandidate);

	// The next swap is armed after this turn: the swap path must not walk the mascot directory.
	if (armCandidate)
		this->armSpeculation(side);

	if (state.highlightOnApply) {
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
	MascotVideoSlot* slot = state.video.data();
	if (!slot)
		return VideoPick::Fallback;
	// One load in flight per panel, the video analogue of the image path's request-in-flight test:
	// a beat or a second timer tick must not supersede a clip that is still opening, and it must
	// not fall through to an image either -- the panel is showing video and is merely busy.
	if (slot->isBusy())
		return VideoPick::Skip;
	// The sink belongs to the delegate, so it does not exist until QML has created it. A load
	// started without one would fail against a live panel that is only a moment behind, so the
	// pick is retried rather than reported: the surface has just been told its slot count, and the
	// delegate it creates in answer to that is what owns the sink.
	MascotSurface* surface = state.surface.data();
	if (!surface || !surface->slotSink(0, 0)) {
		if (state.videoSinkRetries++ < VIDEO_SINK_RETRY_LIMIT) {
			if (!this->warnedVideoSink) {
				this->warnedVideoSink = true;
				this->logVideo(QStringLiteral("no video sink for the panel yet; retrying the pick"));
			}
			QTimer::singleShot(VIDEO_SINK_RETRY_MS, this, [this, side]() {
				if (!this->running)
					return;
				SideState& panel = this->stateFor(side);
				if (panel.shown.path.isEmpty())
					this->pickFor(panel, side, true);
			});
			return VideoPick::Skip;
		}
		// The delegate never appeared: images are the honest degrade, and the log says why once.
		this->logVideo(QStringLiteral("no video sink for the panel after %1 retries; showing images instead")
			.arg(VIDEO_SINK_RETRY_LIMIT));
		return VideoPick::Fallback;
	}
	state.videoSinkRetries = 0;
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
	MascotSurface* surface = state.surface.data();
	MascotVideoSlot* slot = state.video.data();
	if (!surface || !slot || path.isEmpty())
		return;
	// The sinks are bound as late as possible (Task 2's contract): they do not exist until QML has
	// created the delegate, and a binding made when the slot was created would have bound null for
	// the life of the panel.
	slot->setSinks(surface->slotSink(0, 0), surface->slotSink(0, 1));
	// §7.5's ordering, ruled into this task: the output is made visible *before* the clip is asked
	// for, at the opacity it already carries (0 on a fresh panel), so the sink is receiving frames
	// while the first-frame gate waits. Making it visible on the frame instead would have the gate
	// waiting on an output that was not being drawn yet.
	if (!state.videoOutputVisible) {
		surface->setSlotVideoVisible(0, true);
		state.videoOutputVisible = true;
	}
	state.requestedPath = path;
	state.requestedIsVideo = true;
	state.wantedOffsetMs = offsetMs;
	state.requestedAtMs = QDateTime::currentMSecsSinceEpoch();
	++state.videoSeq;
	this->logVideo(QStringLiteral("%1 swap #%2 pool=%3 offset=%4 path=\"%5\"")
		.arg(sideLabel(side))
		.arg(state.videoSeq)
		.arg(this->source.videoPoolSize())
		.arg(offsetMs < 0 ? QStringLiteral("random") : QString::number(offsetMs))
		.arg(path));
	slot->swapTo(path, offsetMs);
}

void MascotController::onVideoFirstFrame(MascotSide side, const QString& path, int ab,
	const QSize& frameSize, qint64 offsetMs)
{
	if (!this->running)
		return;
	SideState& state = this->stateFor(side);
	if (!state.surface || !state.video)
		return;
	// Only the load this panel is waiting for may be committed: a superseded load's frame (a swap
	// that arrived mid-load) is not this panel's media, exactly as a late image delivery is not
	// (amendment H1).
	if (!state.requestedIsVideo || state.requestedPath != path)
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
	this->showMedia(state, side, media, false, false);
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
		if (this->rightState.surface && this->rightState.video)
			this->startMirrorVideo(path, offsetMs);
	}
}

void MascotController::fadeInVideo(SideState& state, MascotSide side, int ab)
{
	if (!state.surface || !state.video || ab < 0 || ab > 1)
		return;
	// A crossfade needs two players. In the clamped mode a swap reloads the output that is on
	// screen, so there is nothing to blend and the duration is 0 -- the hard cut the clamp
	// promises -- rather than a second fade path to keep in step with the first.
	const int fade = this->videoMode == VideoMode::Crossfade ? this->crossfadeMs : 0;
	state.surface->setSlotVideoOpacity(0, ab, 1.0, fade);
	state.surface->setSlotVideoOpacity(0, 1 - ab, 0.0, fade);
	if (fade <= 0) {
		// No fade to wait for, so the outgoing player's decoder is worth nothing: retired now.
		state.video->retireInactive();
		return;
	}
	// The retirement ends the crossfade: until then the outgoing player is what is still partly
	// visible, which is the only reason its decoder is still open. Queued on `this`, so a
	// controller that was stopped in between simply never runs it.
	QTimer::singleShot(fade, this, [this, side]() {
		if (MascotVideoSlot* slot = this->stateFor(side).video.data())
			slot->retireInactive();
	});
}

void MascotController::startMirrorVideo(const QString& path, qint64 offsetMs)
{
	SideState& right = this->rightState;
	if (!right.surface || !right.video || path.isEmpty())
		return;
	// The pair shows one media, so the right panel's pick *is* the left panel's: same file, same
	// offset. A second random start would show two different moments of the same clip, which is
	// the one thing the mirror key exists to prevent.
	this->preparePick(right, false);
	this->startVideoLoad(right, MascotSide::Right, path, offsetMs);
}

void MascotController::onVideoFailed(MascotSide side, const QString& path, bool timedOut)
{
	if (!this->running)
		return;
	SideState& state = this->stateFor(side);
	if (!state.surface || !state.video)
		return;
	// The panel keeps whatever it is drawing. A load that failed says nothing about the clip that
	// is on screen, and a clip that died on screen has already stopped and released itself (Task
	// 2 releases before emitting), so the frame it painted last is still in the slot: blanking
	// here would turn one bad file into a black panel.
	const bool wasLoading = state.requestedIsVideo && state.requestedPath == path;
	if (wasLoading) {
		state.requestedIsVideo = false;
		state.requestedPath.clear();
		state.wantedOffsetMs = -1;
		++state.videoFailures;
	}
	// The other panel may be waiting for this panel's offset to start its copy of the pair. It
	// will never arrive now, and the next committed media re-pairs them.
	if (side == MascotSide::Left && wasLoading) {
		this->rightState.mirrorCopyPending = false;
		this->logVideo(QStringLiteral("the mirrored pair is unpaired for now; the next swap re-pairs it"));
	}
	// One line per unusable file, so a broken clip cannot print once per swap. A timeout is
	// reported separately because it means the file opened and never painted, which is a
	// different fault from "the backend refused it".
	const QString key = path + (timedOut ? QStringLiteral("|t") : QStringLiteral("|f"));
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool repeat = state.lastFailedVideoPath == key
		&& (now - state.lastFailedVideoLogMs) < VIDEO_LOG_INTERVAL_MS;
	if (!repeat) {
		state.lastFailedVideoPath = key;
		state.lastFailedVideoLogMs = now;
		this->logVideo(QStringLiteral("%1 %2 #%3 onScreen=%4 path=\"%5\"")
			.arg(sideLabel(side))
			.arg(timedOut ? QStringLiteral("timed out") : QStringLiteral("failed"))
			.arg(state.videoSeq)
			.arg(state.shown.path.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
			.arg(path));
	}
	// Keep drawing what the panel has, and arm a replacement. A panel that is drawing something is
	// advanced by its clip timer or by the beat, so nothing more is needed; a panel that is drawing
	// nothing would have nothing to advance it, so it asks for a replacement itself -- queued, so
	// this never recurses out of a player's signal, and bounded, so a library the backend cannot
	// decode cannot be walked through at load speed.
	if (!state.shown.path.isEmpty()) {
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
		if (!panel.surface || !panel.shown.path.isEmpty())
			return;
		this->pickFor(panel, side, true);
	}, Qt::QueuedConnection);
}

void MascotController::ensureVideoSlots()
{
	const bool wantVideo = this->mediaMayBeVideo();
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface)
			continue;
		if (wantVideo && !state.video) {
			this->createVideoSlot(state, side);
		}
		else if (!wantVideo && state.video) {
			// The setting is images-only, or the budget cannot carry a panel: the players go, and so
			// does anything they were drawing. The slot itself is repainted by the next pick, which
			// is now an image pick.
			if (state.clipTimer)
				state.clipTimer->stop();
			if (state.videoOutputVisible) {
				state.surface->setSlotVideoVisible(0, false);
				state.videoOutputVisible = false;
			}
			this->destroyVideoSlot(state);
		}
		if (state.video)
			state.video->setSinglePlayer(this->videoMode != VideoMode::Crossfade);
	}
}

void MascotController::createVideoSlot(SideState& state, MascotSide side)
{
	if (!state.surface || state.video)
		return;
	// A child of this controller, not of the surface: releaseSurface() destroys it *before* the
	// surface, because the sinks it draws into belong to the surface's QML delegate.
	auto* slot = new MascotVideoSlot(this);
	// The decoder budget and the clip rules are applied here as well as when they change, so a
	// slot created later (an Images -> Videos settings apply) starts from the current
	// configuration instead of from the class defaults.
	slot->setSinglePlayer(this->videoMode != VideoMode::Crossfade);
	slot->setRandomStart(this->randomStart, this->clipSeconds);
	connect(slot, &MascotVideoSlot::firstFrame, this,
		[this, side](const QString& path, int ab, const QSize& frameSize, qint64 offsetMs) {
			this->onVideoFirstFrame(side, path, ab, frameSize, offsetMs);
		});
	connect(slot, &MascotVideoSlot::failed, this,
		[this, side](const QString& path) { this->onVideoFailed(side, path, false); });
	connect(slot, &MascotVideoSlot::timedOut, this,
		[this, side](const QString& path) { this->onVideoFailed(side, path, true); });
	state.video = slot;
	// A panel built while the window cannot be seen starts paused, exactly like one that was
	// already running when the window went away.
	slot->setPaused(this->videoPaused);
}

void MascotController::destroyVideoSlot(SideState& state)
{
	if (!state.video)
		return;
	// Synchronous, like the surface: the players are detached from the sinks they draw into (which
	// still exist at this point) and then deleted.
	state.video->shutdown();
	delete state.video.data();
	state.video = nullptr;
	state.requestedIsVideo = false;
	state.mirrorCopyPending = false;
	state.wantedOffsetMs = -1;
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
	if (!state.video || state.shown.type != MascotMedia::Video)
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

void MascotController::applyVideoPaused()
{
	if (!this->running)
		return;
	const bool pause = this->videoShouldPause();
	if (pause == this->videoPaused)
		return;
	this->videoPaused = pause;	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (state.video)
			state.video->setPaused(pause);
		if (!state.clipTimer)
			continue;
		if (pause) {
			// Nothing may advance while nothing can be seen: a timer that fired here would start a
			// load whose first frame is what the gate reads as "a swap happened".
			state.clipTimer->stop();
		}
		else if (state.shown.type == MascotMedia::Video && !state.shown.path.isEmpty()) {
			// The clip on screen is resumed by the player itself; its cadence starts again from here.
			this->restartClipTimer(state, side);
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
	const bool leftVideo = this->leftState.shown.type == MascotMedia::Video;
	const bool rightVideo = this->rightState.shown.type == MascotMedia::Video;
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
		// M2: a video wait is not an image wait. The paths cannot collide (one is a mascot image,
		// the other a library video), but a delivery must never be applied to a panel that is
		// waiting for a player's frame, so the flag is checked rather than assumed.
		if (state.requestedIsVideo)
			continue;
		const bool awaited = state.requestedPath == media.path;
		const bool speculative = state.prefetchPath == media.path;
		if (awaited) {
			// showMedia() drops both waits: the media is on screen, so it is not also a candidate
			// for a later swap, and any older pick is stale.
			this->showMedia(state, side, media, false, true);
		}
		else if (speculative) {
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
		// M2: an image decode failing says nothing about the video a panel may be loading, so a
		// panel waiting for a player's frame keeps its wait.
		if (state->requestedIsVideo)
			continue;
		if (state->requestedPath == path)
			state->requestedPath.clear();
		if (state->prefetchPath == path)
			state->prefetchPath.clear();
	}
}
