#pragma once
#include "MascotMedia.h"
#include "MascotMediaSource.h"
// Included rather than forward declared: QPointer<T> needs a complete T, and this class
// holds the surfaces and the prefetcher by QPointer for lifetime reasons (see SideState).
#include "MascotPrefetcher.h"
#include "MascotSurface.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <Qt>

class MainApp;
class MascotImageCache;

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
// Thread affinity: every member, signal and slot here runs on the GUI thread. The only
// cross-thread input is MascotPrefetcher::mediaReady/mediaFailed, which the prefetcher emits
// on the GUI thread after queueing a decode result back.
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
		// The path this panel is waiting for. A media is applied to a panel only while it is
		// what that panel asked for, so a late delivery can never overwrite a newer pick.
		QString requestedPath;
		// What the panel displays, including the palette and content centre it came with.
		MascotMedia shown;
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
	void applyDisplay(SideState& state, MascotSide side);
	void applyMirror(SideState& state, MascotSide side);
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

};
