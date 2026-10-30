#include "stdafx.h"
#include "MascotController.h"
#include "MainApp.h"
#include "utils.h"
#include <QEvent>
#include <QVBoxLayout>

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
	this->clearSide(this->leftState);
	this->clearSide(this->rightState);
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
	// Both panels were empty when the old code ran updateMascots() right before
	// showMascots(); one seeding swap does both jobs now.
	this->nextImages(false);
}

void MascotController::stop()
{
	this->running = false;
	this->disconnectBeats();
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
	// images only; a value this milestone cannot honour is reported rather than ignored.
	const QString mediaKind = config->get("mascots_media");
	if (!mediaKind.isEmpty() && mediaKind.compare(QStringLiteral("Images"), Qt::CaseInsensitive) != 0)
		qWarning() << "mascots_media:" << mediaKind << "is not supported in this build; showing images.";

	this->source.setAllFilesRandom(config->get_bool("mascots_allfiles_random"));
	this->applySeedSource();

	const bool mirror = this->mirrorEnabled();
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (!state->surface)
			continue;
		// The surface-wide default. The per-slot value written by applyDisplay() below is
		// what is drawn, so the default can never disagree with the picture.
		state->surface->setMirror(mirror);
		// The retired renderer's resizeImage() used to re-render from the unflipped original
		// whenever the settings were applied, which dropped a user flip; a config change does
		// the same thing here so the two behave alike.
		state->flipToggled = false;
	}
	// A mode change repaints what is already on screen; the surface default alone would only
	// affect a slot created later.
	this->applyDisplay(this->leftState, MascotSide::Left);
	this->applyDisplay(this->rightState, MascotSide::Right);
	this->updateLayout();
}

void MascotController::applySeedSource()
{
	if (!this->App || !this->App->config)
		return;
	Config* config = this->App->config;
	// The seed string and the two switches the generator read, with the same precedence:
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
	// The old flipPixmap() transformed an empty pixmap into an empty pixmap: flipping a panel
	// that shows nothing is not a state change.
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
	surface->setSlotFillMode(0, MascotSurface::FillMode::HeightFit);
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
	// ===== AMENDMENT Z: TEMPORARY TIMING INSTRUMENTATION - DELETE AFTER THE LATENCY GATE =====
	// One log line per measured swap, with the running maximum, so the latency gate only has to
	// read the last of them. It writes a log entry and nothing else: no media, no pin, no state
	// here is touched.
	connect(surface, &MascotSurface::swapFramePainted, this,
		[this](const QString& label, double msecs) {
			if (!this->App || !this->App->logger)
				return;
			// A panel that is hidden or minimised never repaints, so its measurement would be
			// the interval until some unrelated paint. Those are reported and not counted
			// rather than folded into the maximum.
			static constexpr double SWAP_TIMING_NO_FRAME_MS = 2000.0;
			this->swapTimingSamples += 1;
			if (msecs > SWAP_TIMING_NO_FRAME_MS) {
				this->App->logger->log(QStringLiteral("MASCOT-TIMING-Z sample=%1 commit->painted=no frame within %2 ms, not counted (%3)")
					.arg(this->swapTimingSamples).arg(SWAP_TIMING_NO_FRAME_MS, 0, 'f', 0).arg(label),
					QStringLiteral("MASCOT-TIMING-Z"));
				return;
			}
			if (msecs > this->swapTimingWorstMs)
				this->swapTimingWorstMs = msecs;
			this->App->logger->log(QStringLiteral("MASCOT-TIMING-Z sample=%1 commit->painted=%2 ms max=%3 ms (%4)")
				.arg(this->swapTimingSamples).arg(msecs, 0, 'f', 2)
				.arg(this->swapTimingWorstMs, 0, 'f', 2).arg(label),
				QStringLiteral("MASCOT-TIMING-Z"));
		});
	// ===== END AMENDMENT Z ===================================================================
}

void MascotController::releaseSurface(SideState& state)
{
	if (!state.surface)
		return;
	state.surface->removeEventFilter(this);
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
	state.surface->setSlotMedia(0, state.shown);
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
	// The old flipPixmap() was a toggle over whatever the side rule had produced, and a new
	// image reset it. One authoritative value per slot, written here and nowhere else.
	if (state.flipToggled)
		mirrored = !mirrored;
	state.surface->setSlotMirrored(0, mirrored);
}

void MascotController::requestPath(SideState& state, const QString& path)
{
	state.requestedPath = path;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
}

MascotMedia MascotController::pickFor(SideState& state, MascotSide side, bool updateHighlight)
{
	if (!this->running || !state.surface)
		return MascotMedia();

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

	// ===== AMENDMENT Z: TEMPORARY TIMING INSTRUMENTATION - DELETE AFTER THE LATENCY GATE =====
	// The clock starts at the commit; the surface reports when the frame carrying this media has
	// been painted, which is what "commit -> visible" means for a widget-hosted Qt Quick panel.
	if (this->App && this->App->debug_mode)
		state.surface->beginSwapTiming(QStringLiteral("%1 %2")
			.arg(side == MascotSide::Left ? QStringLiteral("left") : QStringLiteral("right"), media.path));
	// ===== END AMENDMENT Z ===================================================================

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
	const QString path = this->nextSourcePath();
	if (path.isEmpty())
		return;
	state.prefetchPath = path;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->request(path);
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
	this->nextImages(false);
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
		if (state->requestedPath == path)
			state->requestedPath.clear();
		if (state->prefetchPath == path)
			state->prefetchPath.clear();
	}
}
