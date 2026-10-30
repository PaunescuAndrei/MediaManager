#pragma once
#include "definitions.h"
#include <QString>
#include <QStringList>
#include <random>

class MainApp;
class QRandomGenerator;

// Picks the next mascot file, with exactly the selection and seeding behaviour of the retired
// generator's getRandomImagePath() / refreshRngFromConfig() -- same directory walk, same
// shuffle scope, same seed derivation -- and nothing else: no decoding, no queue, no thread.
// This class is the only thing that chooses what the panels show.
//
// It reads no config (amendment D's "a key nothing reads" cuts both ways: the reader is the
// controller): the two config-derived inputs arrive through setSeedSource() and
// setAllFilesRandom(). Paths are not all re-read: the directory list and the flat file list are
// scanned once, at the first pick and again only while the directory list is empty (so a mascots
// folder created after startup is still found) -- which is the retired generator's behaviour,
// staleness for files added to the mascot root included. The files inside the chosen
// subdirectory are re-read on every pick, so a file added to an existing subfolder is seen.
//
// The video pool is the other half of the same job and keeps the same three rules. Its scan is
// one `getVideos` query -- by far the expensive half of a pick -- so it happens once and is
// re-read only after invalidateVideoPool(), which is the only thing that drops it. It reads no
// config either: the weighted/uniform switch arrives through setWeightedChoice() like the other
// two inputs. What parity with getRandomVideoPath() genuinely needs -- the user's filters, the
// per-category bias settings and the seed salt -- is taken from MainWindow through the same
// calls that function made, so the pool is the app's own `Next` rather than an imitation of it.
// The scan sits behind a mutable cache because videoPoolSize() has to be able to answer before
// the first pick: a caller that asks the size to decide whether video is possible would never
// take the pick that fills the pool if the answer were 0 until then.
class MascotMediaSource {
public:
	// `App` is used only for the seed salt (MainWindow::saltSeed) and may be null, in which
	// case the seed is used unsalted -- refreshRngFromConfig()'s null-config branch.
	// (The video pool reads more of it: the database, the current category and MainWindow's
	// filter and bias settings. A null App, or one whose window or database is not up yet, simply
	// has no video pool rather than an empty one.)
	explicit MascotMediaSource(MainApp* App = nullptr);

	// true: the next path comes from the flat list of every file below the mascots directory.
	// false: one random subdirectory first, then one random file below it.
	void setAllFilesRandom(bool allFilesRandom);

	// What refreshRngFromConfig() reads from config, supplied by the caller: the raw
	// `random_seed` value and whether seeding is on (`random_use_seed` && `mascots_use_seed`).
	// The raw value is what the "seed changed" test compares, exactly as the retired generator
	// did -- a changed salt does not re-seed -- and the salted form is only what the RNG is
	// seeded with.
	void setSeedSource(const QString& seedString, bool useSeed);

	// Whether the next video pick is the app's weighted pick (true, the default -- what the `Next`
	// button does) or a plain uniform draw over the same candidates with no bias read at all
	// (false, `mascots_weighted_random` off). One of the two config-derived inputs, supplied by
	// the caller: this class reads no config.
	void setWeightedChoice(bool weighted);

	// The one path a video pick has to avoid: the file the main player holds, while
	// `mascots_exclude_current` asks for it, and empty otherwise. §7.8's only exclusion.
	void setExcludePath(const QString& path);

	// The next mascot path, or an empty string when no mascot directory holds a file.
	QString nextImagePath();

	// The next video path out of the cached pool, or an empty string when the pool is empty (no
	// row passed the filters). Never the excluded path unless the pool leaves no alternative; the
	// pick itself walks no directory and runs no query.
	QString nextVideoPath();

	// Drops the cached candidate list, so the next use re-reads the database once. Called on a
	// model reset, a filter apply and a database switch -- and by nothing else.
	void invalidateVideoPool();

	// How many files the cached pool holds. Answers by scanning the pool if that has not happened
	// yet (see ensureVideoPool), and 0 when the source has no live app to scan against.
	int videoPoolSize() const;

private:
	// The retired generator's initMascotsPaths().
	void initPaths();
	// The retired generator's refreshRngFromConfig().
	void refreshRng();
	// Fills the video pool unless the cached scan is still valid. The only place the database is
	// read, and it leaves the cache uninitialised when there is nothing to scan against (no app,
	// no window, no database) so a source used before the app is up is not left empty for good.
	void ensureVideoPool() const;

	MainApp* App = nullptr;
	bool allFilesRandom = false;
	QStringList mascotsPaths;
	QStringList mascotsAllPaths;
	bool pathsInitialized = false;
	std::mt19937 rng;
	QString seedString;
	bool seedEnabled = false;
	QString lastSeedString;
	bool lastUseSeed = false;
	bool rngInitialized = false;
	// The video side. `weightedChoice` defaults to true, so a caller that never sets it picks the
	// way the `Next` button does.
	bool weightedChoice = true;
	QString excludePath;
	// The cached scan and its validity. Mutable because videoPoolSize() is const and still has to
	// answer; nothing but ensureVideoPool()/invalidateVideoPool() writes them.
	mutable QList<VideoWeightedData> videoPool;
	mutable bool videoPoolInitialized = false;
};
