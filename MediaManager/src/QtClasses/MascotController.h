#pragma once
#include "MascotMedia.h"
#include "MascotMediaSource.h"
// Included rather than forward declared: QPointer<T> needs a complete T, and this class
// holds the surfaces and the prefetcher by QPointer for lifetime reasons (see SideState).
#include "MascotPrefetcher.h"
#include "MascotSurface.h"
// M2, same reason as the three above: SideState holds one of these per panel by QPointer.
#include "MascotVideoSlot.h"
// M3 Task 1: the per-band state lives on MascotBand, one object per band of a panel, which
// SideState owns by unique_ptr. Included rather than forward declared for the reason the two
// above are included: the vector's destructor needs the complete type.
#include "MascotBand.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <Qt>
#include <memory>
#include <vector>

class MainApp;
class MascotImageCache;
class QTimer;

// Which panel a mascot lives on. Every entry point names a side instead of passing the widget
// it used to act on; the left/right asymmetry (the side-directory flip rule) and the QML
// surfaces themselves are the controller's business from here on.
enum class MascotSide { Left, Right };

// Decides what each mascot panel shows and the wiring that turns the old entry points into
// calls on it. One controller drives two surfaces, each of which renders a single slot.
//
// It owns four pieces of state that the retired renderer kept inside the widget:
//   - what a panel is waiting for (`requestedPath`) and what it shows (`shown`);
//   - the per-slot mirror, which is the side-directory rule XOR the user's flips;
//   - the cache pin held for the media a panel displays (amendment H2);
//   - the palette the last applied media carries, which is where the theme colour comes from
//     (amendment H5 -- the cache is not consulted for it and nothing is re-extracted).
//
// M3 Task 1: the request path and the shown media are the per-band state now, held by one
// MascotBand per band of a panel; the controller keeps the decisions that set them (which path
// to ask for, which media to commit, when to mirror) and the band keeps the record and makes
// every write into its slot. The mirror, the pin and the palette stay where they were: they are
// per path and per panel, not per band.
//
// Thread affinity: every member, signal and slot here runs on the GUI thread. The only
// cross-thread input is MascotPrefetcher::mediaReady/mediaFailed, which the prefetcher emits
// on the GUI thread after queueing a decode result back.
//
// M2 adds a second media kind to the same model, not a second model beside it: a panel's type
// is decided per pick, a video pick is a panel waiting for a path exactly like an image pick is,
// and the media that lands either way is committed through the same showMedia() -- so pins, the
// mirror reset, highlightOnApply and the theme-colour emission keep working unchanged. What the
// video branch adds is what an image does not have: the media does not exist until a frame of it
// reaches the sink (so nothing is applied from a pick), its cadence is a clip timer rather than
// the beat, its players are the panel's own, and `mascots_max_decoders` has to fit them.
class MascotController : public QObject {
	Q_OBJECT
public:
	// `cache` is used only to build the surfaces (they hand it to the shared engine's image
	// provider). Pinning goes through the prefetcher, which owns the path -> cache id
	// mapping. All pointers must outlive this object: the cache and the prefetcher are
	// process-lifetime members of MainApp (amendment U) and the hosts are children of the
	// window this controller is a child of.
	MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
		QWidget* leftHost, QWidget* rightHost, QObject* parent = nullptr);
	~MascotController() override;

	// Binds the surfaces this controller drives. Called with the surfaces start() built;
	// public because it is the one place the controller takes its surface references, and
	// because a caller that builds its surfaces itself has to be able to say so.
	void setSurfaces(MascotSurface* left, MascotSurface* right);

	// Enables the panels: builds the surfaces (a user who never enables the renderer pays no
	// Qt Quick cost), applies config, takes the beat connections and seeds both panels.
	void start();
	// Disables them: drops queued work, unpins everything, clears both slots and deletes the
	// surfaces. Idempotent, and safe to call from a shutdown path.
	void stop();
	bool isRunning() const;

	// Re-reads every config value this class consumes and re-applies it to the surfaces
	// (surface default mirror, slot geometry, the centred-content override).
	void applyConfig();
	// Slot geometry is the controller's job (amendment V): each slot is the rectangle of the
	// surface that hosts it. Called on enable, once per host resize and on a mode change.
	void updateLayout();

	// Replaces what `side` shows: served from the panel's speculative candidate when it has one
	// (a committed MascotMedia, no decode), otherwise picked, requested and written when
	// mediaReady delivers it (amendment H1).
	void nextImage(MascotSide side, bool updateHighlight = false);
	// The both-panel swap: a new left media plus either the same media on the right (the
	// mirror key, or `forcedMirror` from the click map) or an independently picked one.
	void nextImages(bool forcedMirror);

	void flipSide(MascotSide side);
	void flipBoth();
	// The side-directory rule: a file whose parent directory is `right` is drawn mirrored on
	// the left panel and vice versa.
	bool flipForSide(const QString& path, MascotSide side) const;
	// The theme palette of the media `side` displays, weighted by area or by dominant area.
	color_area colorFor(MascotSide side, bool weighted);

	// A file dropped on (or debugged into) one panel: shows it unflipped, and with the mirror
	// key on copies it onto the other panel mirrored, which is what the retired MainWindow drop
	// entry point did with setImage(path) / setImage(path, true). Gated on debug mode like that
	// entry point was.
	void showDroppedImage(MascotSide side, const QString& path);

signals:
	// A click on a panel's slot, re-emitted with the side that was clicked.
	void clicked(MascotSide side, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	// The palette of a media that just landed, for the entry points that asked for the theme
	// colour to follow the swap. Emitted only when that was requested.
	void themeColorChanged(color_area color);

protected:
	// The hosting surface's resize is what moves the slot with it: the host receives its new
	// width from the layout after applyMascotWidth() returns, so re-laying out from there
	// would use the old rectangle.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct SideState {
		// QPointer, not raw: a surface can also be destroyed by its host going away (Task 5
		// deletes the old panels next), and a null pointer is answerable while a dangling one
		// is not.
		QPointer<MascotSurface> surface;
		// M3 Task 1: the bands of this panel, one per slot. `shown`, `requestedPath`,
		// `requestedIsVideo`, `video`, `wantedOffsetMs`, `slotFillMode`, `slotFillModeKnown` and
		// `videoOutputVisible` are the band's now; `band(0)` is the one band a single-band panel
		// has, which is the shape Task 2 generalises. A band outlives the surface it draws into
		// (see MascotBand::setSurface), so a panel rebuilt keeps its band object and only loses
		// the slot state that described the old surface.
		std::vector<std::unique_ptr<MascotBand>> bands;
		MascotBand* band(int i) const;
		// The speculative pick: the path requested ahead of the next swap, and the decoded
		// media it produced once it arrives. Exactly one deep -- a queue is deliberately not
		// kept -- and both empty when nothing is armed.
		QString prefetchPath;
		MascotMedia candidate;
		// The user's flip parity since the media last changed: a flip is a toggle, and a new
		// image resets it.
		bool flipToggled = false;
		// Set by a dropped/debugged file, which bypasses the side-directory rule exactly as
		// the old debug path did; cleared by the next pick.
		bool mirrorOverrideEnabled = false;
		bool mirrorOverrideValue = false;
		// Set by a request that wants the theme colour updated when its media lands.
		bool highlightOnApply = false;

		// ---- M2: the video half of one panel ----
		// Set on the right panel while the left panel's pick -- the one the pair mirrors -- is
		// still loading: the copy starts from the left panel's firstFrame(), which is where its
		// offset finally exists.
		bool mirrorCopyPending = false;
		// Counts the video swaps this panel has asked for. A swap line and its first-frame line
		// carry the same number, which is what lets the gate pair them and read the delta.
		quint64 videoSeq = 0;
		// When the outstanding swap was asked for (milliseconds since the epoch), for that delta.
		qint64 requestedAtMs = 0;
		// The clip timer, and the only cadence a video panel has: restarted on every committed
		// video, never started for an image panel (M1's beat-driven cadence is untouched). Owned
		// by the controller, so it survives the enable/disable cycles the surfaces do not.
		QTimer* clipTimer = nullptr;
		// Failures since the last committed media, so a broken clip is not retried forever and a
		// panel that cannot play anything stops asking rather than walking the whole library.
		int videoFailures = 0;
		// How many times this panel has re-asked for a video pick because its QML delegate (and
		// therefore its sink) did not exist yet. A delegate created a turn later is normal; one
		// that never appears has to fall back to images instead of asking forever.
		int videoSinkRetries = 0;
		// Set while a pick is waiting for that retry. nextImages() reads it: a panel that is about
		// to retry is still the panel whose pick the pair mirrors, so the right panel's copy has to
		// be armed now rather than left unpaired.
		bool videoPickRetryPending = false;
		// The same count, for one *specific* load: a mirror copy's file at a fixed offset. A copy
		// that cannot find its sink has to be retried as that same load, because its offset is the
		// pair's invariant and a fresh pick would choose a different one.
		int videoLoadRetries = 0;
		// The last path this panel was told failed, and when it was reported, so one unusable
		// file produces one line instead of one per swap.
		QString lastFailedVideoPath;
		qint64 lastFailedVideoLogMs = 0;
		// When this panel last wrote a media-type line, for the rate-limited ones (pool empty,
		// which branch picked).
		qint64 lastTypeLogMs = 0;
		// The same, for the line that says a preview tooltip is holding this panel's next clip
		// back (§7.13). The rule is re-asked every PREVIEW_DEFER_MS while the tooltip plays, so
		// the line needs its own rate limit: sharing one with the type line would let a deferral
		// suppress the "which branch picked" line the gate reads.
		// (Task 6: that interval is the configured `mascots_busy_defer_ms` now -- the constant the
		// sentence above names was the shipped default, not a name this class still holds. The
		// reason for the separate rate limit is unchanged.)
		qint64 lastPreviewDeferLogMs = 0;
	};

	SideState& stateFor(MascotSide side);
	const SideState& stateFor(MascotSide side) const;
	static MascotSide other(MascotSide side);

	bool mirrorEnabled() const;
	bool centerContentEnabled() const;
	// Feeds the seed string and the two switches into the source. Called when config is
	// applied and again before every pick, because a settings change can land in between.
	void applySeedSource();
	// Feeds the source and returns the next path. The one place a pick is made, so the seed is
	// always as fresh as the config.
	QString nextSourcePath();

	void createSurfaces();
	void configureSurface(SideState& state, MascotSide side);
	void releaseSurface(SideState& state);
	// Clears the slot, releases the pins and forgets the state of one side. Every path that
	// replaces or drops a displayed media goes through here, which is what keeps a pin from
	// outliving what it protects (amendment H2).
	void clearSide(SideState& state);
	// A pinned cache entry protects the id against eviction, and the flag is one per entry
	// while two panels (and their speculative candidates) can reference the same id. A path is
	// pinned exactly while some owner field still names it, so these two keep that invariant:
	// the owner fields are mutated first and every path whose ownership may have changed is
	// then re-evaluated -- which is why a candidate becoming the displayed media never passes
	// through an unpinned moment.
	bool pinReferenced(const QString& path) const;
	void syncPin(const QString& path);
	// Writes the mirror, the media and the centred-content override the config asks for into
	// one slot. Shared by a fresh media and by a config change, so a mode change repaints
	// what is already on screen.
	// M3 Task 1: "into one slot" is the band's slot now and the writes are the band's; this is
	// the decision half, presentMedia() is the write half.
	void applyDisplay(SideState& state, MascotSide side);
	// The write half of applyDisplay(): applies the mirror for `media`, then the fill mode, then
	// the media itself -- the order the slot has always been written in. showMedia() takes this
	// path too, after it has recorded the media on the band and before the slot is written, so
	// the media write never lands under the old mirror.
	void presentMedia(SideState& state, MascotSide side, const MascotMedia& media);
	// `path` is the media the side rule is asked about: showMedia() presents a media between
	// recording it and writing it, so the rule is given the path explicitly instead of reading
	// the band.
	void applyMirror(SideState& state, MascotSide side, const QString& path);
	void requestPath(SideState& state, const QString& path);
	// Commits a media to one panel: pins it, writes the slot, drops whatever that panel was still
	// waiting for, arms the next speculative pick and pushes the theme colour when the swap asked
	// for it. `fromReady` means the media came out of this panel's own candidate, which is then
	// consumed.
	void showMedia(SideState& state, MascotSide side, const MascotMedia& media, bool fromReady,
		bool armCandidate);
	// Stores a decoded speculative media, pinned, until the next swap takes it.
	void holdCandidate(SideState& state, const MascotMedia& media);
	// Requests the path for the panel's *next* swap. Exactly one deep per panel; does nothing
	// while a candidate is held or its decode is in flight.
	void speculate(SideState& state, MascotSide side);
	// Arms that speculation at the end of the current turn, so the swap path itself never walks
	// the mascot directory.
	void armSpeculation(MascotSide side);
	// Serves one panel's next media. Returns the media when the pick could be committed at once
	// (a ready candidate), i.e. with no decode and no request on this path; an empty media means
	// the pick is in flight and will be shown through mediaReady (amendment H1).
	MascotMedia pickFor(SideState& state, MascotSide side, bool updateHighlight);
	// The state a fresh pick starts from, also used when a pick is copied to the other panel.
	void preparePick(SideState& state, bool updateHighlight);

	void connectBeats();
	void disconnectBeats();
	void onSwapBeat();
	void onFlipBeat();
	bool beatsAllowed() const;

	void onMediaReady(const MascotMedia& media);
	void onMediaFailed(const QString& path);

	// ---- M2: the video branch ----
	// What the panels can do with the configured decoder budget. Crossfade is the full design
	// (two players per panel, so one clip fades in while the other fades out); SinglePlayer is
	// the clamp below it (one player per panel, so a swap reloads the output already on screen
	// and there is nothing to fade); Off is a budget too small to give both panels a player.
	enum class VideoMode { Crossfade, SinglePlayer, Off };

	// Which kind of media one panel displays for this pick. Decides from the value applyConfig()
	// cached rather than from the config, for the same reason the source reads none: a settings
	// change must not land one pick late. Its diagnostics are rate-limited per panel.
	MascotMedia::Type pickType(SideState& state);
	// True when the media setting wants video at all (Videos or Mixed). What keeps an images-only
	// configuration free of video work, and what decides whether a clamp line is worth writing.
	bool mediaSettingWantsVideo() const;
	// True when the media setting wants video *and* the decoder budget allows a player.
	bool mediaMayBeVideo() const;
	// Re-reads the pool's invalidation fingerprint -- the database, the filter settings and the
	// model's row count -- and drops the cached scan when any of them moved (§7.8's triggers).
	void invalidateVideoPoolIfStale();

	// Starts one load into `path` at `offsetMs` (-1 means the configured random start) and
	// records the wait. The shared tail of a fresh pick and of a mirror copy.
	void startVideoLoad(SideState& state, MascotSide side, const QString& path, qint64 offsetMs);
	// The video branch of pickFor(). Started means a load is in flight and this pick is done;
	// Skip means the panel already has a load in flight or is about to retry, so nothing must be
	// picked at all; Fallback means video cannot serve this pick and the image path should.
	enum class VideoPick { Started, Skip, Fallback };
	VideoPick pickVideo(SideState& state, MascotSide side, bool updateHighlight);
	void onVideoFirstFrame(MascotSide side, const QString& path, int ab, const QSize& frameSize,
		qint64 offsetMs);
	// A load -- or a clip already on screen -- that failed or timed out. Never blanks the panel:
	// whatever it is drawing keeps playing, and the panel is armed for a replacement.
	// Task 6: `kind` is the slot's own distinction between a decode failure and a wiring report,
	// which is what decides whether the *file* may be dropped for the session (§7.8).
	void onVideoFailed(MascotSide side, const QString& path, MascotVideoSlot::FailKind kind,
		bool timedOut);
	// Fades the output the new clip landed on in, the previous one out, and schedules the
	// outgoing player's retirement for the end of the fade (immediately when there is no fade).
	void fadeInVideo(SideState& state, MascotSide side, int ab);
	// Copies the left panel's committed video onto the right panel: same file, same offset.
	void startMirrorVideo(const QString& path, qint64 offsetMs);

	// ---- M2 (Task 6): the preload ----
	// Chooses the panel's next clip *now* and parks it on the player the next swap will use, so
	// that swap is a play() plus the fade instead of a load. Queued, because it reads the pool and
	// posts a load, and the commit (or the fade that ends it) must not pay for either inline.
	void preloadNext(MascotSide side);
	// Does the work preloadNext() queues: every guard, the pick, and the preload itself.
	void startPreload(MascotSide side);
	// A clip is parked. The mirror pair starts its copy of it here, because this is the moment the
	// offset the right panel has to match exists -- which is the whole reason a preloaded pair can
	// swap in one turn instead of waiting for the left panel's first frame.
	void onVideoPreloaded(MascotSide side, const QString& path, qint64 offsetMs,
		const QSize& frameSize);

	// Builds or drops the player half of each panel from the media setting and the decoder budget.
	// Returns true when a panel that was showing a video lost its players: that panel is drawing
	// nothing and nothing else will fill it, so applyConfig() owes it one pick.
	bool ensureVideoSlots();
	void createVideoSlot(SideState& state, MascotSide side);
	void destroyVideoSlot(SideState& state);
	// Applies `mascots_max_decoders` and writes the clamp line once per change of budget.
	void applyDecoderBudget();
	void restartClipTimer(SideState& state, MascotSide side);
	void onClipTimer(MascotSide side);
	void stopClipTimers();

	// Pauses or resumes both panels' players from the window's state and
	// `mascots_pause_unfocused`. Idempotent, so the event filter can call it on every event.
	void applyVideoPaused();
	bool videoShouldPause() const;
	// True while the preview tooltip the window owns is actually playing (§7.13: "no new clip
	// starts while a preview tooltip is playing"). Read through MainWindow's own QPointer, so a
	// closed or never-opened tooltip is a null pointer here rather than a stale flag; the preview
	// keeps its player and its lifecycle, and this class only ever asks it a question.
	bool previewTooltipBusy() const;
	// The one window whose visibility decides the pause state, watched by this object's filter.
	void watchWindow(QWidget* window);

	// Writes one MASCOT-VIDEO line, debug-gated the way MainApp's MASCOT-CACHE line is: an
	// ordinary run pays nothing, and the gate (which runs with -debug) reads every line.
	void logVideo(const QString& message) const;

	MainApp* App = nullptr;
	MascotImageCache* cache = nullptr;
	// QPointer for the same reason the surfaces use one: the prefetcher is process-lifetime
	// today, but a controller that outlives it must not call into it.
	QPointer<MascotPrefetcher> prefetcher;
	QPointer<QWidget> leftHost;
	QPointer<QWidget> rightHost;
	MascotMediaSource source;
	SideState leftState;
	SideState rightState;
	bool running = false;
	// Set by the first applyConfig() that finds an unsupported `mascots_media`: the warning is a
	// statement about the configuration, so it is made once rather than on every Apply.
	bool warnedUnsupportedMedia = false;
	QMetaObject::Connection swapConnection;
	QMetaObject::Connection flipConnection;

	// ---- M2: the video configuration, all of it written by applyConfig() ----
	// The `mascots_media` value, cached: pickType() decides from this instead of re-reading the
	// config on every swap.
	QString mediaSetting = QStringLiteral("Images");
	// `mascots_clip_seconds`: how long one clip stays on screen. 0 disables the clip timer
	// entirely, which leaves the clip looping and the panel advancing on the existing beat.
	int clipSeconds = 30;
	// `mascots_random_start`: a clip may start anywhere in the file that leaves a clip's worth of
	// it after the offset.
	bool randomStart = true;
	// `mascots_crossfade_ms`, clamped to [0, 10000] as it is read.
	int crossfadeMs = 600;
	// `mascots_max_decoders`, the hard ceiling on live players, and the mode derived from it.
	// The clamp degrades rather than fails: below what two players per panel want it is one
	// player per panel with a hard cut, and below what two panels want it is no video at all.
	int maxDecoders = 6;
	VideoMode videoMode = VideoMode::Crossfade;
	// The budget the clamp line was last written for, so it appears when the budget changes and
	// not on every Apply.
	int loggedDecoderBudget = -1;
	// Set by the first pick that found the panel's video sink missing: a delegate that does not
	// exist yet is a wiring fact, not a per-swap event.
	bool warnedVideoSink = false;
	// Set by the first pick that gave up waiting for that sink. Said once for the same reason, and
	// because the retry budget is per episode: without the latch a panel whose delegate never
	// appears would repeat the line once per episode for as long as it is on screen.
	bool warnedVideoSinkGiveUp = false;
	// `mascots_pause_unfocused`, and the state it last produced: a burst of window events must
	// not repeat the pause/resume lines.
	bool pauseUnfocused = true;
	bool videoPaused = false;
	// `mascots_weighted_random`, pushed into the source with the seed before every video pick.
	bool weightedRandom = true;
	// `mascots_preload`. With it off the video path is exactly what it was before the preload
	// existed: every swap chooses its clip and loads it at the swap, which is also the state a
	// panel falls back to whenever a preload cannot be made. Read here, pushed nowhere -- the
	// decision to preload is this class's, the slot only does what it is told.
	bool preloadEnabled = true;
	// The waits that gate a user-visible wait (§7.11), all read here and pushed into the slots with
	// setTimings(). Values rather than constants because "how long before this clip is declared
	// dead" is a machine-speed judgement: a slow machine legitimately needs longer than a fast one,
	// and the user is the one who knows which they have. Clamped where they are read (applyConfig())
	// and again at the point of use (MascotVideoSlot::setTimings()), so a bad value cannot shorten
	// a deadline down to a frame.
	int swapTimeoutMs = 5000;
	int retryIntervalMs = 150;
	int retryAttempts = 5;
	// `mascots_busy_defer_ms`: how often a pick is re-asked while the preview tooltip plays. This
	// one never leaves this class -- the slot has no part in §7.13's deferral.
	int busyDeferMs = 250;
	// The window whose Show/Hide/activation/minimize state drives the pause. Watched with an
	// event filter installed from updateLayout(), which is where a surface learns its window.
	QPointer<QWidget> watchedWindow;
	// Set by the first activation: before it, "not active" is the startup state of a window on
	// its way to being active, not the user having switched away.
	bool sawWindowActivation = false;
	// The video pool's invalidation fingerprint (§7.8): the database, the filter settings and
	// the model's row count. All three are cheap to compare and all three are a reason for the
	// cached scan to be stale, so the pool is dropped at the first pick that sees one move.
	bool poolFingerprintValid = false;
	QString poolDb;
	int poolRowCount = -1;
	QJsonObject poolFilterJson;

};
