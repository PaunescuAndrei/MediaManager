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

// Which panel a mascot lives on. Every entry point names a side instead of passing the widget
// it used to act on; the left/right asymmetry (the side-directory flip rule) and the QML
// surfaces themselves are the controller's business from here on.
enum class MascotSide { Left, Right };

// How a mascot panel arranges its bands, i.e. what one panel *is* for this milestone.
//   - `Static` is one band covering the whole panel: exactly the picture the panel drew before
//     modes existed, and the default this class falls back to for any value it cannot read.
//   - `Scroll` is one band too, but its content is allowed to be wider than the panel and is
//     panned across it. Task 4 owns the pan; the band is the whole panel either way.
//   - `Stripes` is N bands per panel (`mascots_stripes_left` / `_right`), each one a slot of its
//     own, stacked along the mode's axis. Task 5 adds the orientation, the phase stagger and the
//     per-band decoder budget; Task 2 only lays the bands out and fills them.
// The value is read in MascotController::applyConfig() and cached there, for the reason
// `mascots_media` is: a settings change must not land one pick or one layout late.
enum class MascotMode { Static, Scroll, Stripes };

// The sense `mascots_scroll_direction` asks a panning band for.
//   - `LeftToRight` is the order a pass visits the content in: it starts at the content's near end
//     (its left edge flush with the band's left edge) and ends at its far end. On the vertical
//     axis the same statement is `TopToBottom`, and the two "reverse" values are its opposite.
//     That is the reading the spec's own default gives -- §7.6 says a `Scroll` band's offset
//     animates "0 -> -(contentWidth - panelWidth)", i.e. from the content's left end to its right
//     end -- and it is the only reading under which `LeftToRight` and `TopToBottom` can both mean
//     "forward", which is what Ruling M3-4 needs when a direction's axis has to yield to the
//     mode's (see scrollDirectionReverses()).
//   - The value is read in MascotController::applyConfig() and cached, for the reason
//     `mascots_mode` and `mascots_fill` are: no tick reads the config.
enum class ScrollDirection { LeftToRight, RightToLeft, TopToBottom, BottomToTop };

// The axis a `Stripes` panel stacks its bands along (M3 Task 5), from
// `mascots_stripes_orientation`.
//   - `Horizontal` is the shipped default: the panel is sliced top to bottom into horizontal
//     strips, so a band's panning axis is the horizontal one (its content is normalised to the
//     band's height and the width is what overflows -- `HeightFit`).
//   - `Vertical` slices the panel left to right into columns, so a band's panning axis is the
//     vertical one (normalised to the band's width, the height overflows -- `WidthFit`).
// The stacking axis and the panning axis are perpendicular, which is why the orientation, the
// fill mode and the scroll axis are three answers derived from this one value rather than three
// independent settings. Read in applyConfig() and cached there for the reason `mascots_mode` is:
// no layout path may read the config one Apply late.
enum class StripesOrientation { Horizontal, Vertical };

// Decides what each mascot panel shows and the wiring that turns the old entry points into
// calls on it. One controller drives two surfaces, each of which renders a single slot.
// (M3 Task 2: "renders a single slot" is the M1/M2 shape. A panel now renders one slot per band
// of its mode -- one band in `Static` and `Scroll`, N in `Stripes` -- and each band is a
// MascotBand; the sentence above stays as the record of what the controller drove before a mode
// existed. The panel's *policy* -- picks, the mirror pair, the clip timer, the pins -- is still
// per panel and lives on band 0, which every mode has.)
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
	// (M3 Task 2: "each slot" is each *band's* slot now -- N rectangles for a `Stripes` panel,
	// one full-panel rectangle for `Static` and `Scroll`, which is the rectangle this wrote
	// before modes existed.)
	void updateLayout();

	// ---- M3 Task 2: the layout mode ----
	// The mode value as this class reads it, case-insensitively. `ok` is false for anything that
	// is not one of the three names, and the returned mode is then `Static` -- the panel's
	// historical shape and the documented default -- so a caller can fall back in one step.
	static MascotMode modeFromString(const QString& value, bool* ok);
	// M3 Task 5: `mascots_stripes_orientation` as this class reads it, case-insensitively. `ok`
	// is false for anything that is not `Horizontal` or `Vertical`, and the returned value is then
	// `Horizontal` -- the documented default and the axis every Stripes panel stacked along before
	// the key existed -- so a caller that ignores `ok` still gets a layout that draws.
	static StripesOrientation stripesOrientationFromString(const QString& value, bool* ok);

	// How many bands `side` has in the current mode: one for `Static` and `Scroll`, the
	// configured `mascots_stripes_left`/`_right` for `Stripes`. The number both panels' layouts
	// are derived from, so Task 5's decoder budget can ask it without knowing the mode.
	int bandCount(MascotSide side) const;
	// `bandCount()` equal slices of that panel's rectangle along the mode's stacking axis,
	// rounded so they sum to the panel exactly. Empty while `side` has no surface.
	QVector<QRectF> bandRects(MascotSide side) const;
	// M3 Task 5: how many of `side`'s bands may own a player right now. One per band in `Scroll`
	// and `Stripes`, clamped by `mascots_max_decoders` across both panels; one band per panel in
	// `Static`, where `applyDecoderBudget()` decides whether that band gets two players (crossfade)
	// or one (the M2 clamp). 0 means the panel shows images, which is the degradation a short
	// budget promises: a band is never left blank, it shows a picture.
	// This is the question Task 6's cadence will ask per band; it is the one place a band's
	// player entitlement is recorded.
	int videoBandsFor(MascotSide side) const;
	// Applies the current mode to both panels: resizes each existing surface's slot count,
	// creates or destroys band objects from the tail, applies the rectangles and leaves every
	// surviving band's slot state in place (setSlotCount() re-applies it, so what a surviving
	// band was drawing stays drawn), then fills the bands that have no content. Idempotent, and it
	// never replaces a surface -- the surface is the thing drawing, so it is resized in place.
	// Called from applyConfig(); does nothing while no panel exists yet (the enable path applies
	// the same layout when its surfaces are built).
	void applyMode();

	// ---- M3 Task 4: the scroll driver ----
	// One driver for the whole controller: one QTimer at ~16 ms and one QElapsedTimer, shared by
	// both panels and every band. The timer exists only while a scroll-capable mode is active and
	// the panels can be seen, so a `Static` config -- the default and every task's regression line
	// -- creates no timer, writes no offset and logs nothing at all.
	//
	// The pan itself is arithmetic over a phase, never over pixels: the phase is stored as a
	// fraction of a pass, so a resize rescales the band's position instead of resetting it
	// (Review Focus 8), and the fraction is wrapped with fmod, so a long stall or a slow frame can
	// move a band by at most one travel. A tick writes at most one property per band (through
	// MascotSurface::setSlotScrollOffset(), the one-write path) and never re-seeks, reloads or
	// re-decodes anything.
	void startScrollDriver();
	void stopScrollDriver();
	// The timer's slot. Public like the two above because the plan's Task 4 interface says so; it
	// re-checks every reason not to run before it touches a slot, and it stops the driver rather
	// than running to throw the work away.
	void onScrollTick();
	// The distance the pan can spend for one band, in pixels: `max(0, contentExtent - bandExtent)`
	// on the mode's pan axis, from the size the media is painted at (MascotSurface::
	// slotContentSize(), the one geometry source) and the band's own rectangle. 0 means the content
	// fits its band, so the band stands still and centred (Ruling M3-6) and writes nothing.
	double scrollTravel(const MascotBand& band) const;
	// The direction value as this class reads it, case-insensitively. `ok` is false for anything
	// that is not one of the four names, and the returned value is then `LeftToRight` -- the
	// documented default and the sense a `Scroll` band takes anyway (Ruling M3-4) -- so a caller
	// that ignores `ok` still gets a direction that pans.
	static ScrollDirection scrollDirectionFromString(const QString& value, bool* ok);

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
		// M3 Task 2: the band count this panel's surface was last given. applyMode() compares the
		// mode's count with this so an Apply that changed neither the mode nor a count does
		// nothing at all; -1 until a surface has been configured for this panel.
		int configuredBandCount = -1;
		// ---- M3 Task 4: this panel's scroll state ----
		// The panel's pan phase, in travel lengths of the panel's widest band: 0..1 is one pass and
		// 1..2 is the return leg a pingpong folds back, so one number serves both kinds of seam.
		// A fraction of the travel, never a pixel offset -- that is what makes a resize rescale the
		// pan instead of resetting it (Review Focus 8) and what makes a long stall unable to move a
		// band by more than one travel. One per panel, not per band (the plan's step 1): the bands
		// of a panel are one pass, and Task 5 staggers them with per-band offsets instead of a
		// second clock.
		double scrollPhase = 0.0;
		// One entry per band: the zero-travel line has been written since the last mode change. The
		// line is a statement about the content, so it is written once per band per mode change
		// rather than once per tick (the M2 rate-limit pattern); it is sized with `bands` by
		// syncBands(), and reset by applyMode() on a change of mode.
		// (M3 Task 5: the same latch covers the band whose travel is positive but shorter than one
		// tick's step. Such a band takes the still branch too -- it cannot pan at a frame rate --
		// so it owes the same once-per-layout statement, and the line it writes says which of the
		// two reasons made it stand still.)
		QVector<bool> scrollZeroTravelLogged;
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
	// M3 Task 2: brings one panel's band vector to the mode's band count. Bands are destroyed from
	// the tail (their players come down while the QML sinks they draw into still exist) and
	// created for the new indices; a band whose index survives keeps its media, its request and
	// its players. Writes the surface's slot count as well, so the two can never disagree.
	void syncBands(SideState& state, MascotSide side);
	// M3 Task 2: gives every band of both panels that has no content some, one pick per band,
	// after a layout change. Band 0 takes the panel's existing pick path; the extra bands of a
	// `Stripes` panel are image-only for this task (the per-band player budget is Task 5), so
	// they are filled through the same image request/delivery path and keep that picture.
	void fillEmptyBands();

	// ---- M3 Task 4: the scroll driver's own half ----
	// Starts or stops the driver to match the current state. The one place the "should it be
	// running" question is asked, so every path that can change the answer (enable, disable, a
	// mode change, a window event) reconciles the same way.
	void updateScrollDriver();
	// Whether the driver has anything to do at all: a running controller, a mode with a pan in it,
	// panels a user can see, and the pause rule the players already obey (§7.13 -- no work while
	// hidden or minimized, and `mascots_pause_unfocused` decides whether a merely unfocused window
	// counts).
	bool scrollShouldRun() const;
	// The tick's own guard, and deliberately not scrollShouldRun(): it asks only the reasons that
	// are cheap enough to ask 60 times a second (a Qt visibility flag and the pause rule) and
	// leaves the covered-window test to the event-driven reconciliation, because
	// utils::IsMyWindowVisible() walks every top-level window and builds GDI regions -- more work
	// per call than a whole tick does.
	bool scrollCanTick() const;
	// Whether the panels are on screen, by the same test the beat cadence uses: the surface is
	// visible and its window is visible, unminimized and not covered. `beatsAllowed()` is this
	// answer now, and the scroll driver is its second caller.
	bool panelsVisible() const;
	// The axis the pan travels along, from the mode's stacking axis (Ruling M3-4). `Scroll` is
	// horizontal by definition -- §7.6 sizes its content wider than the panel, and
	// effectiveFillMode() forces the matching HeightFit for it -- and `Stripes` stacks the same way
	// until Task 5 reads `mascots_stripes_orientation`. That key is the only thing this answer
	// ever needs to follow, and nothing else in the driver knows which axis it got.
	// (M3 Task 5: the key is read and cached now. `Stripes` answers vertical when the orientation
	// is `Vertical` -- bands are columns, so the pan runs down each column -- and horizontal
	// otherwise; `Scroll` stays horizontal. The panning axis is perpendicular to the stacking axis,
	// which is why this is not the same question as stripesStackVertically().)
	bool scrollPanIsVertical() const;
	// Whether the configured direction is one of the two that runs the pass backwards. Read by the
	// tick once per frame rather than once per band, and by the axis check in applyConfig().
	bool scrollDirectionReverses() const;
	// Whether the configured direction names the other axis than the mode pans along -- the
	// disagreement Ruling M3-4 resolves in the mode's favour (the sense is kept, the axis is the
	// mode's) with one line, written once per configured value.
	bool scrollDirectionIsVertical() const;
	// A band's share of a pass, in the same travel-length units as the phase (Task 5's
	// `index / count` stagger). 0 is the single-band case and the only value this task produces.
	// (M3 Task 5: `index / count` for every band of a `Stripes` panel, and 0 for the one band of
	// `Static`/`Scroll`. The offsets stay distinct under pingpong even though the reflected path
	// makes two staggered bands coincide for an instant at the fold; scrollPhaseOffset()'s body
	// says why that is accepted rather than normalised.)
	double scrollPhaseOffset(const MascotBand& band) const;
	// Whether this band pans the other way (Task 5's `mascots_stripes_alternate` reversing odd
	// bands). False for every band this task produces, and the global direction is applied on top
	// of it rather than folded into it, so the two can be changed independently.
	// (M3 Task 5: odd `Stripes` bands when the key is on, false everywhere else. The sign is XORed
	// with the configured direction in the tick, so two reversals cancel.)
	bool scrollReversed(const MascotBand& band) const;
	// Writes 0 to every band that still carries an offset, and records it. The one path that takes
	// a pan off the slots when the mode stops panning (`Scroll` -> `Static`): the surface's mirror
	// keeps the last offset and applySlot() would replay it, so a `Static` panel would otherwise
	// draw the picture the pan left it at.
	// (M3 Task 5: applyMode() also calls it on every mode change, not only into `Static`, because
	// the phase is reset there too and a new layout must not start from an offset measured on the
	// old one. Its phase half is then a second write of the 0 applyMode() has just written.)
	void resetScrollOffsets();
	// Writes one MASCOT-SCROLL line, debug-gated exactly like logVideo(): an ordinary run pays
	// nothing, and the gate (which runs with -debug) reads every line. A separate tag from
	// MASCOT-VIDEO so the two families can be counted apart.
	void logScroll(const QString& message) const;
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
	// M3 Task 3: `mascots_fill` as this class reads it, case-insensitively. `ok` is false for
	// anything that is not one of the five names, and the returned mode is then `HeightFit` --
	// what M1 shipped, and what every existing panel already draws (Ruling M3-2) -- so a caller
	// that ignores `ok` still gets a mode that draws something rather than an uninitialised one.
	static MascotSurface::FillMode fillModeFromString(const QString& value, bool* ok);
	// The fill mode the bands of this layout mode are written with. `Static` is the one mode
	// whose band is the whole panel and therefore has no panning axis to normalise, so it honours
	// the configured value; `Scroll` and `Stripes` need the panning axis normalised, which is
	// `HeightFit` for the horizontal axis both of them stack along in this task. M3 Task 5 moves
	// the `Stripes` answer onto `mascots_stripes_orientation` (a vertical stripe stacks along the
	// other axis, so its normalised mode is `WidthFit`).
	// (M3 Task 5: the answer now follows the orientation -- `WidthFit` for a vertical `Stripes`
	// panel, `HeightFit` for a horizontal one -- and `Scroll` stays `HeightFit` because it pans
	// horizontally by definition (Ruling M3-4). Doing only the axis change without this one pins
	// the pan axis: every vertical band would keep `HeightFit`, its content would fit the band's
	// height exactly, and the travel would be 0 on every band.)
	MascotSurface::FillMode effectiveFillMode() const;
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
	// M3 Task 2: `bandIndex` names the band -- and therefore the slot -- being written. Band 0 is
	// the panel's policy band and every pre-mode call site names it.
	void presentMedia(SideState& state, MascotSide side, const MascotMedia& media, int bandIndex);
	// `path` is the media the side rule is asked about: showMedia() presents a media between
	// recording it and writing it, so the rule is given the path explicitly instead of reading
	// the band.
	// M3 Task 2: `bandIndex` is the band whose slot receives the value. The parity itself is the
	// panel's, so every band of a `Stripes` panel is written with the same answer.
	void applyMirror(SideState& state, MascotSide side, const QString& path, int bandIndex);
	// `bandIndex` is the band whose request record is written (M3 Task 2: a `Stripes` panel's
	// extra bands are filled through this same request). The panel-level half of the call --
	// dropping a pending mirror copy -- belongs to the policy band only.
	void requestPath(SideState& state, const QString& path, int bandIndex);
	// Commits a media to one panel: pins it, writes the slot, drops whatever that panel was still
	// waiting for, arms the next speculative pick and pushes the theme colour when the swap asked
	// for it. `fromReady` means the media came out of this panel's own candidate, which is then
	// consumed.
	// M3 Task 2: `bandIndex` names the band the media is committed to. The panel-level policy
	// below -- the candidate, the speculation, the flip parity, the mirror pair, the theme colour
	// -- is band 0's and runs for band 0 only, so a fill of an extra band cannot disturb it.
	void showMedia(SideState& state, MascotSide side, const MascotMedia& media, bool fromReady,
		bool armCandidate, int bandIndex);
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
	// (M3 Task 5: every band of the panel is considered, not only its policy band. A band's player
	// is created when bandMayOwnPlayer() says the budget carries it and destroyed when it does not;
	// a band that loses a player while showing video is downgraded to an image rather than left on
	// a frozen frame.)
	bool ensureVideoSlots();
	void createVideoSlot(SideState& state, MascotSide side, int bandIndex);
	void destroyVideoSlot(SideState& state, int bandIndex);
	void destroyVideoSlot(SideState& state);
	// Applies `mascots_max_decoders` and writes the clamp line once per change of budget.
	void applyDecoderBudget();
	// M3 Task 5: spends the decoder budget across the bands of a panning mode and records the
	// answer videoBandsFor() reports. `Static` is left exactly as M2 had it -- one band per panel,
	// whose one or two players applyDecoderBudget() decides -- so a default config writes nothing
	// and a `Static` run's log stays what merged master's was. The shortfall line is written once
	// per change of budget, band count or allocation (the M2 clamp-line pattern).
	void applyBandBudget();
	// Whether `bandIndex` of `side` may own a player under the current mode and budget: the guard
	// ensureVideoSlots() creates from and the answer Task 6's pick path will ask before it lets a
	// band become a video. `Static` is band 0 only.
	bool bandMayOwnPlayer(MascotSide side, int bandIndex) const;
	// Whether a mode fades between two players at all. Only `Static` does (§7.5): `Scroll` and
	// `Stripes` give a band one player, so a swap reloads the output on screen and there is nothing
	// to blend -- they take the hard cut and never consult `mascots_crossfade_ms`.
	bool videoCrossfades() const;
	// Whether `Stripes` stacks its bands top to bottom (horizontal strips). True for every mode
	// that has one band per panel, where the answer only selects which equivalent slice of the
	// panel `slicedBandRects()` returns. This is the stacking axis; the pan axis is the other one.
	bool stripesStackVertically() const;
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
	// The same latch for `mascots_mode` (M3 Task 2), for the same reason: an unreadable value is a
	// statement about the configuration, and the fallback is `Static` rather than a guess.
	bool warnedUnsupportedMode = false;
	// The same latch for `mascots_fill` (M3 Task 3): an unreadable value is reported once, and the
	// fallback is `HeightFit` -- the mode M1 shipped, not the spec table's original `Crop`.
	bool warnedUnsupportedFill = false;
	// The same latch for `mascots_stripes_orientation` (M3 Task 5): an unreadable value is reported
	// once, and the fallback is `Horizontal` -- the axis every Stripes panel stacked along before
	// the key existed -- so a typo cannot turn the bands on their side.
	bool warnedUnsupportedStripesOrientation = false;
	QMetaObject::Connection swapConnection;
	QMetaObject::Connection flipConnection;

	// ---- M3 Task 2: the layout configuration, written by applyConfig() ----
	// `mascots_mode`, cached: bandCount()/bandRects()/applyMode() answer from this one value, so
	// a settings change cannot land on one panel and not the other, and no layout path re-reads
	// the config.
	MascotMode mascotMode = MascotMode::Static;
	// ---- M3 Task 3: the fill configuration, written by applyConfig() ----
	// `mascots_fill`, cached for the same reason `mascots_mode` is: presentMedia() asks
	// effectiveFillMode() for it, so no repaint path re-reads the config. `HeightFit` is the
	// default and the value every existing panel draws (Ruling M3-2), not the spec table's
	// original `Crop`.
	MascotSurface::FillMode fillModeSetting = MascotSurface::FillMode::HeightFit;
	// `mascots_stripes_left` / `_right`, clamped to [1, 8] where they are read. Only `Stripes`
	// reads them; `Static` and `Scroll` are one band regardless of what they say.
	// (M3 Task 5 owns `mascots_stripes_orientation` and `_alternate`; this task assumes the
	// default stacking axis and no phase stagger, which is what the counts alone describe.)
	// (M3 Task 5: both keys are read now, below. The sentence above stays as the record of what
	// Task 2 asserted; the counts remain the only per-panel difference, and the orientation and
	// the alternate flag apply to both panels alike.)
	int stripesLeftBands = 2;
	int stripesRightBands = 2;
	// ---- M3 Task 5: the stripes configuration, written by applyConfig() ----
	// `mascots_stripes_orientation`, cached: bandRects(), scrollPanIsVertical() and
	// effectiveFillMode() answer from this one value, so the three cannot disagree about the axis.
	// `Horizontal` is the documented default and the axis every Stripe panel had before the key
	// existed, so it is also the fallback for a value this class cannot read.
	StripesOrientation stripesOrientation = StripesOrientation::Horizontal;
	// `mascots_stripes_alternate`: odd bands pan the other way (the woven look). `True` is the
	// documented default; it is a per-band sign on top of `mascots_scroll_direction`, not a second
	// direction setting.
	bool stripesAlternate = true;
	// The mode applyMode() last applied, and whether it has applied one at all. applyMode() is
	// idempotent: an Apply that changed neither the mode nor a panel's band count does nothing.
	MascotMode appliedMode = MascotMode::Static;
	// The orientation the last layout was derived with (M3 Task 5). An orientation change alters
	// the stacking axis without changing the band count, so applyMode() has to compare this too or
	// a `Horizontal` -> `Vertical` switch would leave the old rectangles in place.
	StripesOrientation appliedStripesOrientation = StripesOrientation::Horizontal;
	bool modeApplied = false;
	// ---- M3 Task 5: the per-band decoder allocation, written by applyBandBudget() ----
	// How many bands of each panel may own a player in a panning mode. 0 means the panel shows
	// images; the bands at indices below the count are the ones that keep (or get) a player, so the
	// allocation is a prefix of each panel's band list rather than an arbitrary set. `Static` does
	// not use these -- its one band's players are applyDecoderBudget()'s business.
	int leftBandPlayers = 0;
	int rightBandPlayers = 0;
	// The budget, the mode, the bands that asked and the bands that kept a player when the clamp
	// line was last written, so the line appears once per change of any of them (the M2 clamp-line
	// pattern) rather than once per Apply or once per band. The mode is part of the key because it
	// is part of the sentence: a `Scroll` panel's one band and a `Stripes` panel's one band are the
	// same count but not the same statement.
	int loggedBandBudgetDecoders = -1;
	int loggedBandBudgetWanted = -1;
	int loggedBandBudgetAllowed = -1;
	MascotMode loggedBandBudgetMode = MascotMode::Static;

	// ---- M3 Task 4: the scroll configuration and the driver, all written by applyConfig() ----
	// `mascots_scroll_speed`, in px/s, clamped to [0, MAX_SCROLL_SPEED] where it is read. A
	// negative value is clamped to 0 -- the band stands still -- rather than reversed: the direction
	// key is the only thing that decides the sense, so a sign mistake in the speed cannot silently
	// invert what the user asked the direction to do.
	int scrollSpeed = 40;
	// The value as the user wrote it, so a rate-limited line can echo the configuration rather than
	// a normalised spelling of it, and so the one-line latch can tell one configured value from the
	// next.
	QString scrollDirectionText = QStringLiteral("LeftToRight");
	// `mascots_scroll_direction`, parsed. `LeftToRight` is the default and the fallback, which is
	// also the sense a horizontal pan takes from a vertical direction under Ruling M3-4 -- so a
	// value this class cannot read still pans rather than stopping the band.
	ScrollDirection scrollDirection = ScrollDirection::LeftToRight;
	// `mascots_scroll_pingpong`: reflect the phase at each end instead of wrapping it.
	bool scrollPingpong = false;
	// The direction text the axis line was last written for, so the resolution is said once per
	// configured value and not on every Apply.
	QString loggedScrollDirection;
	// The same latch `mascots_mode` and `mascots_fill` have: an unreadable direction is a statement
	// about the configuration, so it is reported once rather than on every Apply.
	bool warnedUnsupportedScrollDirection = false;
	// The driver's one clock and its one timer. The timer is created on the first start rather than
	// in the constructor, so a config that never scrolls allocates nothing; the clock is a value so
	// that arming it costs no allocation on any path.
	QTimer* scrollTimer = nullptr;
	QElapsedTimer scrollClock;

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
