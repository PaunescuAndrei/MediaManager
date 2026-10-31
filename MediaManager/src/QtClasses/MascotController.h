#pragma once
#include "MascotMedia.h"
#include "MascotMediaSource.h"
// Included rather than forward declared: QPointer<T> needs a complete T, and this class
// holds the surfaces and the prefetcher by QPointer for lifetime reasons (see PanelState).
#include "MascotPrefetcher.h"
#include "MascotSurface.h"
// M2, same reason as the three above: PanelState holds one of these per panel by QPointer.
#include "MascotVideoSlot.h"
// M3 Task 1: the per-band state lives on MascotBand, one object per band of a panel, which
// PanelState owns by unique_ptr. Included rather than forward declared for the reason the two
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
// (M4 Task 1: this is the pair-only identity now. Which panel something belongs to -- its state,
// bands, layout, players and cadence -- is MascotPanel, and a side becomes a panel through
// panelOf(). The paragraph above is kept as the record of what a side named before the split.)
enum class MascotSide { Left, Right };

// M4 Task 1: the per-panel identity -- state lookup, bands, layout, players and cadence are all
// keyed by this. `MascotSide` stays exactly {Left, Right} and is reserved for pair-only policy
// (mirror pairing, flip parity, clicks and drops, highlight, the theme colour): the only
// conversion between the two is side -> panel, through panelOf(), because a panel has no side to
// go back to -- the background is nobody's pair. Where pair code needs a panel's state it asks
// for stateFor(panelOf(side)); the opposite conversion is exactly the unsafe direction this split
// exists to remove.
enum class MascotPanel { Left, Right, Background };

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
	// (M4 Task 2: `backgroundHost` is the window-sized host the background layer is parented to
	// -- MainWindow passes it `centralwidget`, because the layer covers the whole window rather
	// than a column. Like the two column hosts it is a child of that window, so it outlives
	// this controller; unlike them it gets no layout, because updateLayout() positions the
	// surface by hand from the host's rectangle.)
	MascotController(MainApp* App, MascotImageCache* cache, MascotPrefetcher* prefetcher,
		QWidget* leftHost, QWidget* rightHost, QWidget* backgroundHost, QObject* parent = nullptr);
	~MascotController() override;

	// Binds the surfaces this controller drives. Called with the surfaces start() built;
	// public because it is the one place the controller takes its surface references, and
	// because a caller that builds its surfaces itself has to be able to say so.
	void setSurfaces(MascotSurface* left, MascotSurface* right);

	// Enables the panels: builds the surfaces (a user who never enables the renderer pays no
	// Qt Quick cost), applies config, takes the beat connections and seeds both panels.
	// (M4 Task 2: the background layer is built here too, and only while its own switch is on --
	// the invariant syncBackgroundSurface() keeps. The pair is seeded only when `mascots` is on,
	// so a controller started for the background alone does not walk the mascot directory for
	// two panels nobody shows; and a start() that arrives while the background kept this object
	// running rebuilds the pair that stopPanels() took down, which is how the columns come back.)
	void start();
	// Disables them: drops queued work, unpins everything, clears both slots and deletes the
	// surfaces. Idempotent, and safe to call from a shutdown path.
	// (M4 Task 2: the background surface goes the same way, through the same invariant.)
	void stop();
	bool isRunning() const;
	// M4 Task 2: tears down only the two column panels -- their players, their slots, their
	// pins and their surfaces -- and leaves the background layer running. MainWindow::hideMascots()
	// reaches for this when `mascots_background` is on, because turning the columns off must not
	// kill the layer behind them; the full stop() is still what an off background gets. Safe to
	// call while the background is off too: it is then the pair's half of stop().
	void stopPanels();

	// Re-reads every config value this class consumes and re-applies it to the surfaces
	// (surface default mirror, slot geometry, the centred-content override).
	void applyConfig();
	// Slot geometry is the controller's job (amendment V): each slot is the rectangle of the
	// surface that hosts it. Called on enable, once per host resize and on a mode change.
	// (M3 Task 2: "each slot" is each *band's* slot now -- N rectangles for a `Stripes` panel,
	// one full-panel rectangle for `Static` and `Scroll`, which is the rectangle this wrote
	// before modes existed.)
	void updateLayout();
	// M4 Task 6: re-writes the background layer's backdrop from the application palette. Called
	// where the colour can have changed -- from applyConfig(), which is every settings Apply, and
	// once when the surface is built -- because MainWindow::changePalette() is not this class's to
	// edit. The pair's and the columns' surfaces are never touched: the backdrop exists only on the
	// layer, and a surface that never receives a setBackdrop() keeps the QML defaults.
	void refreshBackgroundBackdrop();
	// M5 Task 8: applies `mascots_columns_z` and the veil's alpha to the pair. The layer gets the veil
	// from MainWindow, but the columns cannot be put behind a widget -- their QQuickWidget always
	// composites on top -- so "below the veil" is drawn on the panel instead: the window's colour as the
	// pair's backdrop, with the content faded to what the veil would have left of it. Above (the default)
	// is no backdrop and full opacity, which is how the pair has always been drawn.
	void refreshColumnsVeil();

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
	// (M4 Task 2: the background's answer is derived from its *players*, not from its mode's band
	// count: 0 when `backgroundPlayers` is 0 -- no decoders means no background at all -- 1 for
	// `Static`/`Scroll` when it has any player, and `min(backgroundStripes, backgroundPlayers)`
	// for `Stripes`. That is what makes losing a player retire the bands that were spending it
	// through syncBands(), instead of leaving a band with nothing behind it.)
	int bandCount(MascotPanel panel) const;
	// `bandCount()` equal slices of that panel's rectangle along the mode's stacking axis,
	// rounded so they sum to the panel exactly. Empty while `side` has no surface.
	QVector<QRectF> bandRects(MascotPanel panel) const;
	// M3 Task 5: how many of `side`'s bands may own a player right now. One per band in `Scroll`
	// and `Stripes`, clamped by `mascots_max_decoders` across both panels; one band per panel in
	// `Static`, where `applyDecoderBudget()` decides whether that band gets two players (crossfade)
	// or one (the M2 clamp). 0 means the panel shows images, which is the degradation a short
	// budget promises: a band is never left blank, it shows a picture.
	// This is the question Task 6's cadence will ask per band; it is the one place a band's
	// player entitlement is recorded.
	int videoBandsFor(MascotPanel panel) const;
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
	double scrollTravel(MascotPanel panel, const MascotBand& band) const;
	// The direction value as this class reads it, case-insensitively. `ok` is false for anything
	// that is not one of the four names, and the returned value is then `LeftToRight` -- the
	// documented default and the sense a `Scroll` band takes anyway (Ruling M3-4) -- so a caller
	// that ignores `ok` still gets a direction that pans.
	static ScrollDirection scrollDirectionFromString(const QString& value, bool* ok);

	// Replaces what `side` shows: served from the panel's speculative candidate when it has one
	// (a committed MascotMedia, no decode), otherwise picked, requested and written when
	// mediaReady delivers it (amendment H1).
	// (M3 Task 6A: this is the policy band's (band 0's) entry point. Every other band of a
	// `Stripes` panel advances through pickFor() with its own index, or through nextBandImages()
	// when the pair has to move together.)
	void nextImage(MascotSide side, bool updateHighlight = false);
	// The both-panel swap: a new left media plus either the same media on the right (the
	// mirror key, or `forcedMirror` from the click map) or an independently picked one.
	void nextImages(bool forcedMirror);
	// M3 Task 6A: the same pair swap for one band index. `nextImages()` is this call with index 0
	// -- the band a one-band panel has -- so `Static` keeps running exactly the code it ran
	// before, and a `Stripes` panel's band i gets the same pair rule (same file, same offset)
	// applied to its own band instead of to band 0.
	void nextBandImages(int bandIndex, bool forcedMirror);

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
	// ---- M4 Task 1: one panel's parsed layout ----
	// The values applyConfig() caches for one panel. Splitting them out of the controller is what
	// lets the background be configured independently of the columns while every layout path still
	// answers from a cached value and never re-reads the config. The two columns share one instance
	// (`columnsLayout`) because they are one mirror pair; the background owns `backgroundLayout`,
	// which nothing draws from until Task 2 adds its surface.
	// (M4 Task 2: `backgroundLayout` draws now -- bandCount(), bandRects(), effectiveFillMode(),
	// stripesStackVertically() and videoCrossfades() all answer from it for the background, and
	// syncBands() lays its bands out from it. It stays a separate instance exactly because it is
	// not the pair's layout.)
	struct PanelLayout {
		// `mascots_mode`, cached: bandCount()/bandRects()/applyMode() answer from this one value, so
		// a settings change cannot land on one panel and not the other, and no layout path re-reads
		// the config.
		MascotMode mode = MascotMode::Static;
		// ---- M3 Task 3: the fill configuration, written by applyConfig() ----
		// `mascots_fill`, cached for the same reason `mascots_mode` is: presentMedia() asks
		// effectiveFillMode() for it, so no repaint path re-reads the config. `HeightFit` is the
		// default and the value every existing panel draws (Ruling M3-2), not the spec table's
		// original `Crop`.
		MascotSurface::FillMode fill = MascotSurface::FillMode::HeightFit;
		// The mode applyMode() last applied, and whether it has applied one at all. applyMode() is
		// idempotent: an Apply that changed neither the mode nor a panel's band count does nothing.
		MascotMode appliedMode = MascotMode::Static;
		bool applied = false;
		// ---- M3 Task 5: the stripes configuration, written by applyConfig() ----
		// `mascots_stripes_orientation`, cached: bandRects(), scrollPanIsVertical() and
		// effectiveFillMode() answer from this one value, so the three cannot disagree about the axis.
		// `Horizontal` is the documented default and the axis every Stripe panel had before the key
		// existed, so it is also the fallback for a value this class cannot read.
		StripesOrientation orientation = StripesOrientation::Horizontal;
		// The orientation the last layout was derived with (M3 Task 5). An orientation change alters
		// the stacking axis without changing the band count, so applyMode() has to compare this too or
		// a `Horizontal` -> `Vertical` switch would leave the old rectangles in place.
		// (M3 Task 5 fix: the rectangles are written by applyConfig()'s later unconditional
		// updateLayout() -- the orientation is cached before applyMode() runs -- so this comparison is
		// what makes applyMode() run at all on an axis-only change: without it the old offset would
		// stay on the slot and the zero-travel latch would keep a statement about the old layout. The
		// sentence above is kept as the record of what the task first asserted.)
		StripesOrientation appliedOrientation = StripesOrientation::Horizontal;
		// `mascots_stripes_alternate`: odd bands pan the other way (the woven look). `True` is the
		// documented default; it is a per-band sign on top of `mascots_scroll_direction`, not a second
		// direction setting.
		bool alternate = true;
		// ---- M3 Task 4: the scroll configuration, written by applyConfig() ----
		// `mascots_scroll_speed`, in px/s, clamped to [0, MAX_SCROLL_SPEED] where it is read. A
		// negative value is clamped to 0 -- the band stands still -- rather than reversed: the direction
		// key is the only thing that decides the sense, so a sign mistake in the speed cannot silently
		// invert what the user asked the direction to do.
		// (M4 Task 1: a `double` so the background's rate parses into the same shape; the column
		// value is still the integer clampedConfigInt() returns, so its arithmetic is unchanged.)
		double scrollSpeed = 40;
		// `mascots_scroll_direction`, parsed. `LeftToRight` is the default and the fallback, which is
		// also the sense a horizontal pan takes from a vertical direction under Ruling M3-4 -- so a
		// value this class cannot read still pans rather than stopping the band.
		ScrollDirection scrollDirection = ScrollDirection::LeftToRight;
		// `mascots_scroll_pingpong`: reflect the phase at each end instead of wrapping it.
		bool scrollPingpong = false;
	};

	struct PanelState {
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
		// ---- M4 Task 6: the background marquee's own pass ----
		// `Scroll` on the background is a seamless marquee of N cells, not a content pan inside one
		// band, so its offset is a pixel displacement rather than a fraction of a travel: the cells
		// translate by `speed * dt` along the surface's axis and wrap at `N * cellLen`. It is kept
		// per panel like every other pass, and the pair's `scrollPhase` above is untouched by it.
		double marqueeOffset = 0.0;
		// Pingpong's direction latch: +1/-1, flipped once per complete cycle (the distance below
		// reaching one `total`). Seeded at +1 so the configured direction decides the first leg;
		// the modulo placement makes the flip invisible at the seam.
		double marqueePingpongSign = 1.0;
		// How far the current pingpong leg has travelled, so "one complete cycle" is a distance
		// rather than a phase. Only written while `mascots_background_scroll_pingpong` is on; a
		// non-pingpong marquee wraps forever and never reads it.
		double marqueeLegDistance = 0.0;
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
		// (M3 Task 6A: the candidate and its prefetched path are the *band's* now, one record per
		// band (MascotBand::Playback), because a `Stripes` panel gives every band its own next
		// media and band 0's predecoded swap must not be consumed by a stripe's fill.)
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
		// (M3 Task 6A: the per-band half of this block -- the mirrored copy wait, the video
		// sequence, the request timestamp, the retry counters and the clip timer -- moved to
		// MascotBand::Playback and MascotBand's own timer, because a `Stripes` panel gives every
		// band a player and a cadence of its own. What remains here are the two latches below,
		// which are statements about the *panel* -- the pool, the branch that served a pick and
		// the preview tooltip -- rather than about one band.)
		//
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
		// (M3 Task 6A: both latches stay panel-wide. The two statements they rate-limit are about
		// the panel's configuration and the window's tooltip, not about one band, so four stripes
		// share one line instead of writing four.)
		qint64 lastPreviewDeferLogMs = 0;
	};

	PanelState& stateFor(MascotPanel panel);
	const PanelState& stateFor(MascotPanel panel) const;
	// M4 Task 1: the only conversion between the two enums. There is deliberately no inverse: a
	// panel does not have a side to go back to, because the background belongs to no pair.
	static MascotPanel panelOf(MascotSide side);
	// The parsed layout of one panel. Both column panels share `columnsLayout` -- the mirror pair
	// is one layout -- and the background owns `backgroundLayout`.
	PanelLayout& layoutFor(MascotPanel panel);
	const PanelLayout& layoutFor(MascotPanel panel) const;
	// `mascots_stripes_left` / `_right` / `mascots_background_stripes`, the one per-panel value a
	// PanelLayout does not hold: the counts are not layout keys, they are how many bands a panel
	// stacks.
	int stripesCountFor(MascotPanel panel) const;
	// M4 Task 1: the pair side that owns `state`, used where per-panel work reaches pair-only
	// policy (a click, a drop, the theme colour). The background owns neither pair state, so it
	// answers false and the pair policy does not apply to it. This asks the two pair states which
	// one they are; it never converts a panel back into a side.
	bool pairSideOf(const PanelState& state, MascotSide* side) const;
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
	void configureSurface(PanelState& state, MascotPanel panel);
	void releaseSurface(PanelState& state);
	// M3 Task 2: brings one panel's band vector to the mode's band count. Bands are destroyed from
	// the tail (their players come down while the QML sinks they draw into still exist) and
	// created for the new indices; a band whose index survives keeps its media, its request and
	// its players. Writes the surface's slot count as well, so the two can never disagree.
	void syncBands(PanelState& state, MascotPanel panel);
	// M3 Task 2: gives every band of both panels that has no content some, one pick per band,
	// after a layout change. Band 0 takes the panel's existing pick path; the extra bands of a
	// `Stripes` panel are image-only for this task (the per-band player budget is Task 5), so
	// they are filled through the same image request/delivery path and keep that picture.
	// (M3 Task 6A: every band takes the panel's own pick path now, through its own index, so a
	// `Stripes` panel fills each band with whatever `mascots_media` asks for -- a video when the
	// band has a player -- instead of always giving the extra bands a still. Under the mirror key
	// the pair is filled from the left panel's picks, and only a right-panel band past the left
	// panel's count is filled on its own.)
	void fillEmptyBands();

	// ---- M4 Task 2: the background surface's own lifecycle ----
	// Keeps the invariant the layer is defined by: the background surface exists if and only if
	// this controller is running *and* `mascots_background` is on. Called from applyConfig() (so a
	// settings Apply turns the layer on and off live) and from start()/stop(); everything that
	// builds or drops one goes through here, so no other path has to know the rule.
	void syncBackgroundSurface();
	// Builds the window-sized surface: parented to backgroundHost, with no layout (updateLayout()
	// positions it from the host's rectangle), lowered beneath the layout's widgets and shown. The
	// bands are the funded prefix bandCount() derives from backgroundPlayers, so a surface with no
	// player behind it is simply an empty one.
	void createBackgroundSurface();
	// Drops it in the pair's order and for the pair's reason: the bands' players are shut down
	// first (they draw into QVideoSinks the surface's QML delegates own), then clearSide() drops
	// the slots and the pins, then releaseSurface() deletes it.
	void destroyBackgroundSurface();
	// (M4 review fix: existence is not the same question as visibility. A surface with no funded band
	// draws nothing, and nothing -- for this widget -- is an opaque black rectangle: the layer takes
	// the quick widget's opaque blit (see MascotSurface's constructor), which edge-to-edge video
	// hides and an empty layer does not. So the layer is shown only while bandCount() is above zero,
	// and a layer that loses its last player goes back to being invisible instead of black. Called
	// wherever the funded prefix can change: the end of createBackgroundSurface() and the Apply that
	// re-spends the budget.)
	void updateBackgroundVisibility();

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
	// (M4 Task 2: this delegates to anySurfaceVisible(), because the background is now a surface
	// that can be on screen on its own -- with `mascots` off and `mascots_background` on, beats and
	// clip timers must still run. Nothing else in this class uses the pair-only reading, so the
	// old meaning is kept as a name rather than as a second copy of the question.)
	bool panelsVisible() const;
	// M4 Task 2: whether any surface this controller drives -- either column panel or the
	// background layer -- is on screen by the test panelsVisible() has always used. The helper the
	// cadence and the scroll driver ask, so neither has to grow a background case of its own.
	bool anySurfaceVisible() const;
	// M4 Task 2: whether any surface that currently exists is in a mode with a pan in it. The
	// scroll driver's mode gate, per panel now: the background has its own layout, so "the columns
	// are `Static`" is no longer the whole answer to whether there is anything to pan.
	bool panModeActive() const;
	// The axis the pan travels along, from the mode's stacking axis (Ruling M3-4). `Scroll` is
	// horizontal by definition -- §7.6 sizes its content wider than the panel, and
	// effectiveFillMode() forces the matching HeightFit for it -- and `Stripes` stacks the same way
	// until Task 5 reads `mascots_stripes_orientation`. That key is the only thing this answer
	// ever needs to follow, and nothing else in the driver knows which axis it got.
	// (M3 Task 5: the key is read and cached now. `Stripes` answers vertical when the orientation
	// is `Vertical` -- bands are columns, so the pan runs down each column -- and horizontal
	// otherwise; `Scroll` stays horizontal. The panning axis is perpendicular to the stacking axis,
	// which is why this is not the same question as stripesStackVertically().)
	bool scrollPanIsVertical(MascotPanel panel) const;
	// Whether the configured direction is one of the two that runs the pass backwards. Read by the
	// tick once per frame rather than once per band, and by the axis check in applyConfig().
	bool scrollDirectionReverses(MascotPanel panel) const;
	// Whether the configured direction names the other axis than the mode pans along -- the
	// disagreement Ruling M3-4 resolves in the mode's favour (the sense is kept, the axis is the
	// mode's) with one line, written once per configured value.
	bool scrollDirectionIsVertical(MascotPanel panel) const;
	// A band's share of a pass, in the same travel-length units as the phase (Task 5's
	// `index / count` stagger). 0 is the single-band case and the only value this task produces.
	// (M3 Task 5: `index / count` for every band of a `Stripes` panel, and 0 for the one band of
	// `Static`/`Scroll`. The offsets stay distinct under pingpong even though the reflected path
	// makes two staggered bands coincide for an instant at the fold; scrollPhaseOffset()'s body
	// says why that is accepted rather than normalised.)
	double scrollPhaseOffset(MascotPanel panel, const MascotBand& band) const;
	// Whether this band pans the other way (Task 5's `mascots_stripes_alternate` reversing odd
	// bands). False for every band this task produces, and the global direction is applied on top
	// of it rather than folded into it, so the two can be changed independently.
	// (M3 Task 5: odd `Stripes` bands when the key is on, false everywhere else. The sign is XORed
	// with the configured direction in the tick, so two reversals cancel.)
	bool scrollReversed(MascotPanel panel, const MascotBand& band) const;
	// Writes 0 to every band that still carries an offset, and records it. The one path that takes
	// a pan off the slots when the mode stops panning (`Scroll` -> `Static`): the surface's mirror
	// keeps the last offset and applySlot() would replay it, so a `Static` panel would otherwise
	// draw the picture the pan left it at.
	// (M3 Task 5: applyMode() also calls it on every mode change, not only into `Static`, because
	// the phase is reset there too and a new layout must not start from an offset measured on the
	// old one. Its phase half is then a second write of the 0 applyMode() has just written.)
	// (M4 Task 4: one panel at a time, named by the caller. The reset is a statement about *that*
	// panel's pass -- applyMode() calls it for the panel whose own mode just changed -- so a
	// background mode change no longer clears the columns' offsets or their phase, and a columns
	// mode change no longer snaps the layer's pass back to its near end.)
	void resetScrollOffsets(MascotPanel panel);
	// Writes one MASCOT-SCROLL line, debug-gated exactly like logVideo(): an ordinary run pays
	// nothing, and the gate (which runs with -debug) reads every line. A separate tag from
	// MASCOT-VIDEO so the two families can be counted apart.
	void logScroll(const QString& message) const;
	// Clears the slot, releases the pins and forgets the state of one side. Every path that
	// replaces or drops a displayed media goes through here, which is what keeps a pin from
	// outliving what it protects (amendment H2).
	void clearSide(PanelState& state);
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
	MascotSurface::FillMode effectiveFillMode(MascotPanel panel) const;
	// Writes the mirror, the media and the centred-content override the config asks for into
	// one slot. Shared by a fresh media and by a config change, so a mode change repaints
	// what is already on screen.
	// M3 Task 1: "into one slot" is the band's slot now and the writes are the band's; this is
	// the decision half, presentMedia() is the write half.
	void applyDisplay(PanelState& state, MascotPanel panel);
	// The write half of applyDisplay(): applies the mirror for `media`, then the fill mode, then
	// the media itself -- the order the slot has always been written in. showMedia() takes this
	// path too, after it has recorded the media on the band and before the slot is written, so
	// the media write never lands under the old mirror.
	// M3 Task 2: `bandIndex` names the band -- and therefore the slot -- being written. Band 0 is
	// the panel's policy band and every pre-mode call site names it.
	void presentMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, int bandIndex);
	// `path` is the media the side rule is asked about: showMedia() presents a media between
	// recording it and writing it, so the rule is given the path explicitly instead of reading
	// the band.
	// M3 Task 2: `bandIndex` is the band whose slot receives the value. The parity itself is the
	// panel's, so every band of a `Stripes` panel is written with the same answer.
	// The flip parity of one panel's media. The background has no pair to mirror with, so it is
	// never flipped; left and right answer exactly as flipForSide() does.
	bool mirrorParityFor(MascotPanel panel, const QString& path) const;
	void applyMirror(PanelState& state, MascotPanel panel, const QString& path, int bandIndex);
	// `bandIndex` is the band whose request record is written (M3 Task 2: a `Stripes` panel's
	// extra bands are filled through this same request). The panel-level half of the call --
	// dropping a pending mirror copy -- belongs to the policy band only.
	void requestPath(PanelState& state, const QString& path, int bandIndex);
	// Commits a media to one panel: pins it, writes the slot, drops whatever that panel was still
	// waiting for, arms the next speculative pick and pushes the theme colour when the swap asked
	// for it. `fromReady` means the media came out of this panel's own candidate, which is then
	// consumed.
	// M3 Task 2: `bandIndex` names the band the media is committed to. The panel-level policy
	// below -- the candidate, the speculation, the flip parity, the mirror pair, the theme colour
	// -- is band 0's and runs for band 0 only, so a fill of an extra band cannot disturb it.
	// (M3 Task 6A: the candidate, the speculation and the mirror-copy wait are the *band's* now,
	// so `fromReady` consumes the committed band's own candidate and a commit arms that band's own
	// speculation. The flip parity stays the panel's -- a stripe commit must not clear a flip the
	// user made on the panel -- and the theme colour stays band 0's, because the accent is the
	// panel's and four stripes swapping would strobe it. A commit on an extra band under the
	// mirror key also copies the media onto the right panel's same-index band here, which is where
	// the pair rule extends from one band to N.)
	void showMedia(PanelState& state, MascotPanel panel, const MascotMedia& media, bool fromReady,
		bool armCandidate, int bandIndex);
	// Stores a decoded speculative media, pinned, until the next swap takes it.
	// (M3 Task 6A: the candidate belongs to the band the speculation was armed for, so this takes
	// the band rather than the panel's state.)
	void holdCandidate(MascotBand& band, const MascotMedia& media);
	// Requests the path for the panel's *next* swap. Exactly one deep per panel; does nothing
	// while a candidate is held or its decode is in flight.
	// (M3 Task 6A: one band's *next* swap, exactly one deep per band, for the reason the sentence
	// above gives -- a `Stripes` panel gives every band its own next media.)
	void speculate(PanelState& state, MascotPanel panel, int bandIndex);
	// Arms that speculation at the end of the current turn, so the swap path itself never walks
	// the mascot directory.
	void armSpeculation(MascotPanel panel, int bandIndex);
	// Serves one panel's next media. Returns the media when the pick could be committed at once
	// (a ready candidate), i.e. with no decode and no request on this path; an empty media means
	// the pick is in flight and will be shown through mediaReady (amendment H1).
	// (M3 Task 6A: one band's next media, named by its index.)
	MascotMedia pickFor(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	// The state a fresh pick starts from, also used when a pick is copied to the other panel.
	// (M3 Task 6A fix: `bandIndex` gates the four panel-wide values it resets -- the mirror
	// override, the flip parity and the pending theme-colour request -- exactly as showMedia()
	// gates the same values on band 0, so a stripe's pick cannot clear a debug drop's override or
	// another request's highlight before band 0's media lands. Only the policy band's pick starts
	// a new panel media; a stripe's starts that stripe's.)
	void preparePick(PanelState& state, int bandIndex, bool updateHighlight);

	void connectBeats();
	void disconnectBeats();
	// (M3 Task 6B: one handler for the one pulse. A swap beat advances the video bands that have
	// been up for at least half a clip -- image bands always may advance -- and flips when none
	// may; every other beat flips both panels.)
	void onBeat(bool swapWanted);
	// (M3 Task 6B: whether this band's content has been up for the half-clip floor -- or longer
	// than any floor when `mascots_clip_seconds` is 0, which is what makes the beat the whole
	// cadence. Read by onBeat() through the band's committedAtMs.)
	// (M3 Task 6B fix: only a band showing video is rate-limited this way; an image band has no
	// clip of its own and always passes, so a default `Images` run keeps the dice as its only
	// rate limit.)
	bool bandMayAdvance(const MascotBand& band) const;
	// (M3 Task 6B: whether this band can still reach a wrap seam while the scroll driver runs.
	// False means there is no pass to defer to -- the driver is stopped, the speed is 0, the
	// content does not overflow, or the tick would take the still branch -- so onBeat() commits
	// the change now instead of queueing it where it would wait forever.)
	bool bandReachesSeam(MascotPanel panel, const MascotBand& band) const;
	// (M3 Task 6B: the two halves of a beat's advance. scheduleBeatAdvance() queues the change on
	// the band when a seam exists and commits it now otherwise; commitBeatAdvance() is the one
	// action -- it captures only a side and an index, resolves the band fresh, and never reaches
	// applyConfig() or configureSurface().)
	void scheduleBeatAdvance(MascotPanel panel, int bandIndex);
	void commitBeatAdvance(MascotPanel panel, int bandIndex);
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
	// (M4 Task 2: `panel` is asked because the answer is now per panel -- the background is
	// video-only whatever `mascots_media` says, while the pair panels answer from the setting and
	// their own player allocation. The state alone could not distinguish them: the background's
	// state is just a third PanelState.)
	MascotMedia::Type pickType(PanelState& state, MascotPanel panel);
	// True when the media setting wants video at all (Videos or Mixed). What keeps an images-only
	// configuration free of video work, and what decides whether a clamp line is worth writing.
	bool mediaSettingWantsVideo() const;
	// True when the media setting wants video *and* the decoder budget allows a player.
	// (M4 Task 2: per panel, because the background is video-only and has its own allocation: a
	// pair panel answers exactly as before -- the setting plus an allocation -- while the
	// background answers from its own funded band prefix and ignores `mascots_media` entirely.
	// Every caller asks about the panel it is working on.)
	bool mediaMayBeVideo(MascotPanel panel) const;
	// Re-reads the pool's invalidation fingerprint -- the database, the filter settings and the
	// model's row count -- and drops the cached scan when any of them moved (§7.8's triggers).
	void invalidateVideoPoolIfStale();

	// Starts one load into `path` at `offsetMs` (-1 means the configured random start) and
	// records the wait. The shared tail of a fresh pick and of a mirror copy.
	// (M3 Task 6A: `bandIndex` is the band whose player carries the load, and whose request
	// record remembers it. A frame can then only ever be committed to the band that asked.)
	void startVideoLoad(PanelState& state, MascotPanel panel, int bandIndex, const QString& path,
		qint64 offsetMs);
	// The video branch of pickFor(). Started means a load is in flight and this pick is done;
	// Skip means the panel already has a load in flight or is about to retry, so nothing must be
	// picked at all; Fallback means video cannot serve this pick and the image path should.
	enum class VideoPick { Started, Skip, Fallback };
	VideoPick pickVideo(PanelState& state, MascotPanel panel, int bandIndex, bool updateHighlight);
	// (M3 Task 6A: every one of these is told which band reported, so a frame from band 2 can
	// never be committed to band 0. The slot objects are connected per band by createVideoSlot().)
	void onVideoFirstFrame(MascotPanel panel, int bandIndex, const QString& path, int ab,
		const QSize& frameSize, qint64 offsetMs);
	// A load -- or a clip already on screen -- that failed or timed out. Never blanks the panel:
	// whatever it is drawing keeps playing, and the panel is armed for a replacement.
	// Task 6: `kind` is the slot's own distinction between a decode failure and a wiring report,
	// which is what decides whether the *file* may be dropped for the session (§7.8).
	// (M3 Task 6A: a failed band re-picks its own index, under its own failure budget, instead of
	// leaving the stripe blank or on a still for the session.)
	void onVideoFailed(MascotPanel panel, int bandIndex, const QString& path,
		MascotVideoSlot::FailKind kind, bool timedOut);
	// Fades the output the new clip landed on in, the previous one out, and schedules the
	// outgoing player's retirement for the end of the fade (immediately when there is no fade).
	void fadeInVideo(PanelState& state, MascotPanel panel, int bandIndex, int ab);
	// Copies the left panel's committed video onto the right panel: same file, same offset.
	// (M3 Task 6A: onto the right panel's `bandIndex`, because the pair rule is per band index now.)
	void startMirrorVideo(int bandIndex, const QString& path, qint64 offsetMs);

	// ---- M2 (Task 6): the preload ----
	// Chooses the panel's next clip *now* and parks it on the player the next swap will use, so
	// that swap is a play() plus the fade instead of a load. Queued, because it reads the pool and
	// posts a load, and the commit (or the fade that ends it) must not pay for either inline.
	void preloadNext(MascotPanel panel);
	// Does the work preloadNext() queues: every guard, the pick, and the preload itself.
	void startPreload(MascotPanel panel);
	// A clip is parked. The mirror pair starts its copy of it here, because this is the moment the
	// offset the right panel has to match exists -- which is the whole reason a preloaded pair can
	// swap in one turn instead of waiting for the left panel's first frame.
	// (M3 Task 6A: only band 0 preloads; the index is carried so the pair step of a preloaded
	// commit still names the band it belongs to.)
	void onVideoPreloaded(MascotPanel panel, int bandIndex, const QString& path, qint64 offsetMs,
		const QSize& frameSize);

	// Builds or drops the player half of each panel from the media setting and the decoder budget.
	// Returns true when a panel that was showing a video lost its players: that panel is drawing
	// nothing and nothing else will fill it, so applyConfig() owes it one pick.
	// (M3 Task 5: every band of the panel is considered, not only its policy band. A band's player
	// is created when bandMayOwnPlayer() says the budget carries it and destroyed when it does not;
	// a band that loses a player while showing video is downgraded to an image rather than left on
	// a frozen frame.)
	bool ensureVideoSlots();
	void createVideoSlot(PanelState& state, MascotPanel panel, int bandIndex);
	void destroyVideoSlot(PanelState& state, int bandIndex);
	void destroyVideoSlot(PanelState& state);
	// Applies `mascots_max_decoders` and writes the clamp line once per change of budget.
	void applyDecoderBudget();
	// M3 Task 5: spends the decoder budget across the bands of a panning mode and records the
	// answer videoBandsFor() reports. `Static` is left exactly as M2 had it -- one band per panel,
	// whose one or two players applyDecoderBudget() decides -- so a default config writes nothing
	// and a `Static` run's log stays what merged master's was. The shortfall line is written once
	// per change of budget, band count or allocation (the M2 clamp-line pattern).
	void applyBandBudget();
	// M4 Task 2: spends what is left of `mascots_max_decoders` after applyDecoderBudget() and
	// applyBandBudget() have given the pair exactly the entitlement they always had -- columns
	// first, background last. `Static` background mode spends up to two players on its one band
	// (a crossfade), `Scroll` and `Stripes` spend one per band, and the answer is recorded in
	// backgroundPlayers/backgroundCrossfades so bandCount() can derive the background's bands from
	// it. A background with no player left gets none of these and draws nothing (the zero-player
	// line). One MASCOT-BG line per change of the tuple, never per Apply.
	// (M4 Task 4: in the panning background modes this task adds, the subtraction is what the
	// pair's own allocation actually spends. A pair that cannot show video -- `mascots` off or an
	// images-only setting, where `videoBandsFor()` answers 0 for it -- takes none of the budget
	// here, so the layer is offered all of `mascots_max_decoders` and the background's `Stripes`
	// count is funded from it. A `Static` background keeps Task 2's subtraction exactly, because
	// its acceptance is byte-identity. The pair's allocation and lines are untouched: this is only
	// the subtraction.)
	void applyBackgroundBudget();
	// M4 Task 2: the background's band count, derived from the players it was given: 0 with no
	// player, 1 for `Static`/`Scroll` with one, and the funded prefix
	// `min(mascots_background_stripes, backgroundPlayers)` for `Stripes`. bandCount() and
	// videoBandsFor() both answer with it, so the two can never disagree about a band existing.
	int backgroundBandCount() const;
	// M4 Task 6: whether `panel` draws the background marquee -- the background in `Scroll` with at
	// least two funded bands. One funded band is deliberately not a marquee: dividing by `N - 1`
	// would be a division by zero, and a single cell covering the window is the same picture with no
	// translation and no wrap, so bandRects() answers it the full surface and the tick leaves it
	// alone.
	bool backgroundMarqueeActive(MascotPanel panel) const;
	// Whether `bandIndex` of `side` may own a player under the current mode and budget: the guard
	// ensureVideoSlots() creates from and the answer Task 6's pick path will ask before it lets a
	// band become a video. `Static` is band 0 only.
	bool bandMayOwnPlayer(MascotPanel panel, int bandIndex) const;
	// Whether a mode fades between two players at all. Only `Static` does (§7.5): `Scroll` and
	// `Stripes` give a band one player, so a swap reloads the output on screen and there is nothing
	// to blend -- they take the hard cut and never consult `mascots_crossfade_ms`.
	// (M4 Task 2: the background's answer is its own -- backgroundCrossfades, set by
	// applyBackgroundBudget() from its mode and its own player count -- while the pair panels keep
	// exactly the answer above. The background's budget is spent separately, so its crossfade
	// cannot be inferred from the columns' videoMode.)
	bool videoCrossfades(MascotPanel panel) const;
	// Whether `Stripes` stacks its bands top to bottom (horizontal strips). True for every mode
	// that has one band per panel, where the answer only selects which equivalent slice of the
	// panel `slicedBandRects()` returns. This is the stacking axis; the pan axis is the other one.
	bool stripesStackVertically(MascotPanel panel) const;
	void restartClipTimer(MascotPanel panel, int bandIndex);
	void onClipTimer(MascotPanel panel, int bandIndex);
	void stopClipTimers();
	// M3 Task 6A: whether band `bandIndex` is one the mirror key pairs with the left panel. True
	// for every index the left panel has; a right-panel band numbered past the left panel's count
	// has no counterpart to mirror, so it lives its own life (its own picks, its own clip timer).
	bool bandMirrorsLeft(int bandIndex) const;
	// M3 Task 6A: asks for a fresh media for one band after a failure, bounded per band. Queued on
	// `this`, so it never recurses out of a player's signal; `onlyIfEmpty` keeps a band that is
	// drawing something from being replaced when only a blank band owed the retry, while a failed
	// video attempt on a band left showing a still may replace that still (a stripe has no beat to
	// move it on).
	// (M3 Task 6A fix: this is also the single content entry point for a band that has just been
	// given a player -- the budget-increase and Images -> Videos re-fill -- and it is mirror-aware:
	// a mirrored right band never picks on its own, and a mirrored left band advances the pair
	// through nextBandImages(). It replaces the separate media-changed re-fill method.)
	void queueBandRefill(MascotPanel panel, int bandIndex, bool onlyIfEmpty);

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
	// M4 Task 2: writes one MASCOT-BG line, gated exactly like logVideo()/logScroll(): an
	// ordinary run pays nothing, and a run with -debug reads every line. A tag of its own so the
	// background's allocation and gating can be read apart from the columns' video lines, and so
	// a run with `mascots_background` off writes neither.
	void logBackground(const QString& message) const;

	MainApp* App = nullptr;
	MascotImageCache* cache = nullptr;
	// QPointer for the same reason the surfaces use one: the prefetcher is process-lifetime
	// today, but a controller that outlives it must not call into it.
	QPointer<MascotPrefetcher> prefetcher;
	QPointer<QWidget> leftHost;
	QPointer<QWidget> rightHost;
	// M4 Task 2: the window-sized host (MainWindow's `centralwidget`). The background surface is
	// its child and is positioned by hand from its rectangle -- no layout is installed on it, so
	// nothing else can size or move the layer.
	QPointer<QWidget> backgroundHost;
	MascotMediaSource source;
	PanelState leftState;
	PanelState rightState;
	// M4 Task 1: the background panel's state. It stays empty -- no surface and no bands -- until
	// Task 2 builds them; every per-panel function already tolerates both.
	// (M4 Task 2: this is the surface and bands' owner now. They exist exactly while `running &&
	// backgroundEnabled`, which is the invariant syncBackgroundSurface() keeps; the state itself
	// outlives both, exactly as leftState/rightState do.)
	PanelState backgroundState;
	bool running = false;
	// M3 Task 6A: non-zero while a wrap-point commit a band queued is running. A commit is content
	// work, and it must not tear the layout down from inside the scroll driver's band loop; the
	// commits are run after that loop (onScrollTick() collects them), and applyMode() re-queues
	// itself while this is non-zero, so a commit that reaches the one layout entry a content path
	// could call cannot rebuild the band vector underneath a tick. Nothing else in the class may
	// write it.
	int wrapCommitDepth = 0;
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
	// M5 Task 4: the same one-warning latch for the four name-valued background keys, so a typo in
	// `mascots_background_mode` / `_fill` / `_stripes_orientation` / `_scroll_direction` is reported
	// once per process instead of being replaced by the parser's fallback silently. The columns'
	// latches above are untouched: these four describe the layer, not the pair.
	bool warnedUnsupportedBackgroundMode = false;
	bool warnedUnsupportedBackgroundFill = false;
	bool warnedUnsupportedBackgroundStripesOrientation = false;
	bool warnedUnsupportedBackgroundScrollDirection = false;
	// (M3 Task 6B: one connection for the pulse. The two legacy signals stay on the thread for any
	// other listener, but this controller no longer connects them, so one beat is handled once.)
	QMetaObject::Connection beatConnection;

	// ---- M3 Task 2: the layout configuration, written by applyConfig() ----
	// (M4 Task 1: the values themselves are PanelLayout's now -- one instance for the column pair
	// and one for the background -- so this section only says where they live. The comments that
	// described each value moved with it, into PanelLayout.)
	PanelLayout columnsLayout;
	PanelLayout backgroundLayout;
	// `mascots_stripes_left` / `_right`, clamped to [1, 8] where they are read. Only `Stripes`
	// reads them; `Static` and `Scroll` are one band regardless of what they say.
	// (M3 Task 5 owns `mascots_stripes_orientation` and `_alternate`; this task assumes the
	// default stacking axis and no phase stagger, which is what the counts alone describe.)
	// (M3 Task 5: both keys are read now, below. The sentence above stays as the record of what
	// Task 2 asserted; the counts remain the only per-panel difference, and the orientation and
	// the alternate flag apply to both panels alike.)
	int stripesLeftBands = 2;
	int stripesRightBands = 2;
	// M4 Task 1: `mascots_background_stripes`, clamped to [1, 8] like the two column counts. Only
	// the background's `Stripes` mode reads it; the surface that draws from it arrives in Task 2.
	// (M4 Task 2: the surface draws from it now. `Stripes` is the only mode whose band count it
	// decides -- bandCount(Background) clamps it by the players the background was actually given.)
	int backgroundStripes = 3;
	// M4 Task 1: `mascots_background` / `mascots_background_opacity` (the latter clamped to [0, 1]).
	// The background surface that consumes them is added later in this milestone; they are parsed
	// now so the keys have exactly one reader and a settings change cannot land one layout late.
	// (M4 Task 2: the surface is syncBackgroundSurface()'s now, `mascots_background` is one half of
	// the invariant that decides whether it exists, and `mascots_background_opacity` is applied
	// through MascotSurface::setSurfaceOpacity() on every apply while the surface exists. Zero is
	// deliberately not special-cased: it is presentation, and no band decision reads it.)
	// (M5 Task 8: `mascots_background_opacity` left this class. It is the alpha of the veil MainWindow
	// paints over the layer now, and the layer is drawn at full strength -- applying it here as well
	// was the double dim this refactor deleted.)
	bool backgroundEnabled = false;
	// M4 Task 2: `mascots` (the two column panels). Cached here because start() has to know which
	// layer it is enabling: the pair is seeded only when the columns are on, and a start() that
	// brings the columns back under a running background seeds them then. Read here under the
	// same "one reader" rule as every key above.
	bool columnsEnabled = true;
	// ---- M4 Task 2: the background's decoder allocation, written by applyBackgroundBudget() ----
	// What is left of `mascots_max_decoders` after the pair's entitlement: columns first, background
	// last. `backgroundPlayers` is the number of players the layer may spend -- two on a `Static`
	// band (the crossfade) and one per band in `Scroll`/`Stripes` -- and `backgroundCrossfades` is
	// the one answer videoCrossfades(Background) reports. bandCount(Background) derives the bands
	// from `backgroundPlayers`, so a budget that shrinks retires bands through syncBands() instead
	// of leaving one with nothing behind it.
	int backgroundPlayers = 0;
	bool backgroundCrossfades = false;
	// The tuple the allocation line was last written for -- the budget, the players and the bands
	// they bought -- so the line appears once per change (the M2 clamp-line pattern) rather than
	// once per Apply. `loggedBackgroundPlayers` at -1 means nothing has been said yet, which is
	// also what a disabled background resets the latches to, so re-enabling states its piece.
	int loggedBackgroundDecoders = -1;
	int loggedBackgroundPlayers = -1;
	int loggedBackgroundBands = -1;
	MascotMode loggedBackgroundMode = MascotMode::Static;
	// Set by the first pick that found the video pool empty for the background, so the empty-pool
	// line is one per episode rather than one per beat. Cleared by the next background load that
	// actually starts, which is the next time the pool had something to give.
	bool backgroundEmptyPoolLogged = false;
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
	// (M3 Task 5 fix: the per-panel split is part of the tuple too, because two allocations can
	// want and keep the same total on different panels -- a live 2/2 -> 3/1 change with an
	// unchanged total used to leave the log advertising the old split. The whole tuple is also
	// reset by the `Static` branch: a `Static` interlude has no panning allocation to describe, so
	// the next panning mode must say its piece again.)
	int loggedBandBudgetDecoders = -1;
	int loggedBandBudgetWanted = -1;
	int loggedBandBudgetAllowed = -1;
	int loggedBandBudgetLeft = -1;
	int loggedBandBudgetRight = -1;
	MascotMode loggedBandBudgetMode = MascotMode::Static;

	// ---- M3 Task 4: the scroll configuration and the driver, all written by applyConfig() ----
	// (M4 Task 1: the speed, the direction and the pingpong switch moved into PanelLayout, one
	// copy per panel; the text and the latches below stay here because they describe the column
	// keys' diagnostics, which no other panel writes.)
	// The value as the user wrote it, so a rate-limited line can echo the configuration rather than
	// a normalised spelling of it, and so the one-line latch can tell one configured value from the
	// next.
	QString scrollDirectionText = QStringLiteral("LeftToRight");
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
	// (M3 Task 6B: `mascots_swap_on_beat`, cached like every other key this class consumes. False
	// makes a swap beat flip both panels instead of advancing any band, which is the old
	// non-swap-beat behaviour applied to every beat.)
	bool swapOnBeat = true;
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
