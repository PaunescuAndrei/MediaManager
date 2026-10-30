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
	this->clearSide(this->leftState, MascotSide::Left);
	this->clearSide(this->rightState, MascotSide::Right);
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
	this->nextImages(true, false);
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
	this->clearSide(this->leftState, MascotSide::Left);
	this->clearSide(this->rightState, MascotSide::Right);
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
		// customGraphicsView::resizeImage() used to re-render from the unflipped original
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

void MascotController::nextImage(MascotSide side, bool useCache, bool updateHighlight)
{
	this->pickFor(this->stateFor(side), useCache, updateHighlight);
}

void MascotController::nextImages(bool useCache, bool forcedMirror)
{
	if (!this->running)
		return;
	const bool picked = this->pickFor(this->leftState, useCache, true);
	if (this->mirrorEnabled() || forcedMirror) {
		// Both panels show the same media, each drawn by its own side rule. The path copied is
		// the one the left panel is about to show, not the one it still shows: the media has
		// not arrived yet (H1), and copying the old one would leave the panels apart.
		if (!picked || this->leftState.requestedPath.isEmpty() || !this->rightState.surface)
			return;
		this->preparePick(this->rightState, false);
		this->requestPath(this->rightState, this->leftState.requestedPath);
	}
	else {
		this->pickFor(this->rightState, useCache, false);
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
	// customGraphicsView::getColor()'s semantics, unchanged, on the palette the decode already
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
	SideState& state = this->stateFor(side);
	if (!state.surface)
		return;
	// MainWindow::setMascotDebug()'s behaviour: the dropped file is drawn unflipped on this
	// panel (setImage(path)), and with the mirror key on it is copied onto the other panel
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
	// A mascot panel is a tall strip: the old renderer scaled the image to the panel's height
	// and cropped it horizontally, which is Crop's behaviour for the images a mascot folder
	// holds. Fit would letterbox the strip instead.
	surface->setSlotFillMode(0, MascotSurface::FillMode::Crop);
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

void MascotController::clearSide(SideState& state, MascotSide side)
{
	if (state.surface)
		state.surface->clearSlot(0);
	// Amendment H2: the pin follows the slot, so it is dropped exactly when the last panel
	// showing that media stops showing it. A pinned entry is never evicted, which is why a
	// missed unpin is invisible until the cache has grown past every bound.
	this->releasePin(state, side);
	state.requestedPath.clear();
	state.shown = MascotMedia();
	state.flipToggled = false;
	state.mirrorOverrideEnabled = false;
	state.mirrorOverrideValue = false;
	state.highlightOnApply = false;
}

void MascotController::holdPin(SideState& state, MascotSide side, const QString& path)
{
	if (path.isEmpty())
		return;
	state.pinnedPath = path;
	// One flag per cache entry, two panels that can show the same media: if the other panel
	// already holds this id, the entry is pinned and pinning it again would only lose the
	// distinction between "one owner" and "two".
	if (this->stateFor(other(side)).pinnedPath == path)
		return;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->pin(path, true);
}

void MascotController::releasePin(SideState& state, MascotSide side)
{
	const QString path = state.pinnedPath;
	state.pinnedPath.clear();
	if (path.isEmpty())
		return;
	// The other panel still shows it, so the entry stays pinned for that panel.
	if (this->stateFor(other(side)).pinnedPath == path)
		return;
	MascotPrefetcher* source = this->prefetcher.data();
	if (source)
		source->pin(path, false);
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

bool MascotController::pickFor(SideState& state, bool useCache, bool updateHighlight)
{
	if (!this->running || !state.surface)
		return false;

	// MascotPrefetcher::takeReady() is deliberately not used. The old "use cache" path popped
	// a pre-decoded image because decoding used to block the GUI thread; the prefetcher
	// decodes off-thread and announces every result through mediaReady, which is what drives
	// the swap (amendment H1). Pulling the same media here would apply what mediaReady has
	// already applied -- showing every image twice -- and could re-show a media whose pixels
	// the cache has since evicted. Both values therefore behave identically at this level: a
	// request is served from memory when the prefetcher still holds the pixels, and neither
	// value ever waits for a decode.
	Q_UNUSED(useCache);

	// mascotsGeneratorThread::refreshRngFromConfig() re-read the seed on every pick, and a
	// settings change can land between two picks, so the source is fed at the same point
	// rather than only when applyConfig() runs.
	this->applySeedSource();
	const QString path = this->source.nextImagePath();
	if (path.isEmpty())
		return false;

	this->preparePick(state, updateHighlight);
	this->requestPath(state, path);
	return true;
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

void MascotController::applyMedia(SideState& state, MascotSide side, const MascotMedia& media)
{
	if (!state.surface)
		return;

	// Amendment H2: the id this slot is about to display is pinned, and the id it stops
	// displaying is released. Pinning happens after the prefetcher inserted the pixels (it
	// inserts before it announces), so the pin always lands on an existing entry.
	if (state.pinnedPath != media.path)
		this->releasePin(state, side);
	this->holdPin(state, side, media.path);

	state.shown = media;
	state.flipToggled = false;
	this->applyDisplay(state, side);

	if (state.highlightOnApply) {
		state.highlightOnApply = false;
		emit this->themeColorChanged(this->colorFor(side, true));
	}
}

void MascotController::connectBeats()
{
	if (this->swapConnection)
		return;
	if (!this->App || !this->App->MascotsAnimation)
		return;
	// Amendment W: the controller takes the beat connections itself. MainApp's old handlers
	// were gated on leftImg->isVisible(), and that widget is hidden after the cutover, so a
	// branch inside them could never run.
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
	this->nextImages(true, false);
}

void MascotController::onFlipBeat()
{
	if (!this->running || !this->beatsAllowed())
		return;
	this->flipBoth();
}

bool MascotController::beatsAllowed() const
{
	// MainApp's handlers used to test `leftImg->isVisible() && utils::IsMyWindowVisible()`;
	// the surface is that widget's replacement, so the same two conditions apply to it.
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
	// A media is applied only to a panel that is waiting for exactly that path. A delivery
	// nobody asked for (a republish of a media another panel pulled, or one that arrived
	// while stopped) is dropped rather than shown, which is what makes a request a request and
	// not a promise (amendment H1).
	for (MascotSide side : { MascotSide::Left, MascotSide::Right }) {
		SideState& state = this->stateFor(side);
		if (!state.surface || state.requestedPath != media.path)
			continue;
		state.requestedPath.clear();
		this->applyMedia(state, side, media);
	}
}

void MascotController::onMediaFailed(const QString& path)
{
	// Nothing arrives for this path, so nothing is applied: the panel keeps what it has and
	// the next beat re-rolls. Clearing the wait is what keeps a later successful delivery of
	// the same path from being applied to a panel that has moved on.
	for (SideState* state : { &this->leftState, &this->rightState }) {
		if (state->requestedPath == path)
			state->requestedPath.clear();
	}
}
