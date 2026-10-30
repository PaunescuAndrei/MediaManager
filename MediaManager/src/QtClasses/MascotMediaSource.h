#pragma once
#include <QString>
#include <QStringList>
#include <random>

class MainApp;

// Picks the next mascot file, with exactly the selection and seeding behaviour of
// mascotsGeneratorThread::getRandomImagePath() / refreshRngFromConfig() -- same directory
// walk, same shuffle scope, same seed derivation -- and nothing else: no decoding, no queue,
// no thread. The generator thread still exists for the parts of the app that have not moved,
// but from Task 4 on this class is the only thing that chooses what the panels show.
//
// It reads no config (amendment D's "a key nothing reads" cuts both ways: the reader is the
// controller): the two config-derived inputs arrive through setSeedSource() and
// setAllFilesRandom(), and every path is re-read from the directory tree on the same terms
// the generator used, so a folder added while the app runs is picked up the same way.
class MascotMediaSource {
public:
	// `App` is used only for the seed salt (MainWindow::saltSeed) and may be null, in which
	// case the seed is used unsalted -- refreshRngFromConfig()'s null-config branch.
	explicit MascotMediaSource(MainApp* App = nullptr);

	// true: the next path comes from the flat list of every file below the mascots directory.
	// false: one random subdirectory first, then one random file below it.
	void setAllFilesRandom(bool allFilesRandom);

	// What refreshRngFromConfig() reads from config, supplied by the caller: the raw
	// `random_seed` value and whether seeding is on (`random_use_seed` && `mascots_use_seed`).
	// The raw value is what the "seed changed" test compares, exactly as the generator did --
	// a changed salt does not re-seed -- and the salted form is only what the RNG is seeded
	// with.
	void setSeedSource(const QString& seedString, bool useSeed);

	// The next mascot path, or an empty string when no mascot directory holds a file.
	QString nextImagePath();

private:
	// mascotsGeneratorThread::initMascotsPaths().
	void initPaths();
	// mascotsGeneratorThread::refreshRngFromConfig().
	void refreshRng();

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
};
