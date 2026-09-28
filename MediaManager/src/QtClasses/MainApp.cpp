#include "stdafx.h"
#include "MainApp.h"
#include "version.h"
#include "MainWindow.h"
#include "Models/VideosModel.h"
#include "stylesQt.h"
#include "utils.h"
#include "shobjidl_core.h"
#include <QSoundEffect>
#include <QFontDatabase>
#include <atomic>
#include "timeapi.h"
#include "generalEventFilter.h"
#include "TooltipEventFilter.h"
#include "LogDialog.h"
#include <QErrorMessage>
#include "shokoAPI.h"
#include "BasePlayer.h"
#include "definitions.h"

MainApp::MainApp(int& argc, char** argv) : QApplication(argc,argv)
{
	timeBeginPeriod(2);
	bool start_hidden = false;
	if (argc >= 2) {
		for (int i = 0; i < argc; i++) {
			if (strcmp("-debug", argv[i]) == 0)
				this->debug_mode = true;
			if (strcmp("-hidden", argv[i]) == 0)
				start_hidden = true;
		}
	}

	this->logger = new Logger(this,512);
	qMainApp->logger->log("Application Starting.", "INFO");
	this->ErrorDialog = new QErrorMessage();
	this->config = new Config("config.ini");

	if (this->config->get_bool("single_instance")) {
		this->startSingleInstanceServer(QString::fromStdString(utils::getAppId(VERSION_TEXT)));
	}

	if (!SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&(this->uiAutomation))))
	{
		this->logger->log("Failed to create instance of UI automation!", "CRITICAL");
		throw std::runtime_error("Failed to create instance of UI automation!");
	}
	this->logger->log("UI automation instance created successfully.", "INFO");

	videoTypes = this->config->get("video_types").split(',', Qt::SkipEmptyParts);
	svTypes = this->config->get("sv_types").split(',', Qt::SkipEmptyParts);

	this->createMissingDirs();

	QApplication::setQuitOnLastWindowClosed(false);
	this->setStyle("Fusion");
	this->setPalette(get_palette("main"));

	QFontDatabase fontDatabase;
	fontDatabase.addApplicationFont(":/fonts/resources/euphorigenic.ttf");

	this->currentDB = this->config->get("current_db");
	this->db = new sqliteDB(DATABASE_PATH,"mainapp_con");
	this->shoko_API = new shokoAPI("http://localhost:8111");

	if (this->config->get_bool("music_on"))
		this->musicPlayer = new MusicPlayer(this);
	else
		this->musicPlayer = nullptr;
	this->soundPlayer = new SoundPlayer(this, this->config->get("sound_effects_volume").toInt());
	this->soundPlayer->effects_playpause = this->config->get_bool("sound_effects_playpause");
	this->soundPlayer->effect_chance = this->config->get("sound_effects_chance_playpause").toInt()/100.0;
	this->soundPlayer->effects_chain_playpause = this->config->get_bool("sound_effects_chain_playpause");
	this->soundPlayer->effect_chain_chance = this->config->get("sound_effects_chain_chance").toInt() / 100.0;
	if (this->config->get_bool("sound_effects_on"))
		this->soundPlayer->start();

	this->MascotsExtractColor = this->config->get_bool("mascots_color_theme");

	this->VW = new VideoWatcherQt(this);

	this->BpmManager = new CalculateBpmManager(this->config->get("bpm_threads").toInt(), this);
	connect(this->BpmManager, &CalculateBpmManager::bpmCalculated, this, [this](int id, double bpm) {
		this->db->updateBpm(id, bpm);
		if (this->mainWindow) {
            QString path = this->mainWindow->pathById(id);
            if (path.isEmpty()) path = QStringLiteral("ID: %1").arg(id);
            this->logger->log(QStringLiteral("Calculated BPM: %1 for %2").arg(QString::number(bpm), path), "BPM");
            
			if (this->mainWindow->videosModel) {
				QPersistentModelIndex idx = this->mainWindow->modelIndexById(id);
				if (idx.isValid()) {
					this->mainWindow->videosModel->setBpmAtRow(idx.row(), bpm);
				}
			}
		}
	});

	this->mainWindow = new MainWindow(nullptr,this);

	this->MascotsGenerator = new mascotsGeneratorThread(this, MASCOTS_PATH, this->config->get_bool("mascots_allfiles_random"));
	this->MascotsGenerator->start();
	this->MascotsAnimation = new mascotsAnimationsThread(this,this->config->get_bool("mascots_random_change"),this->config->get("mascots_random_chance").toInt()/100.0);
	this->MascotsAnimation->start();

	connect(this->MascotsAnimation, &mascotsAnimationsThread::updateMascotsSignal, this, [this] {
		if(this->mainWindow->ui.leftImg->isVisible() and utils::IsMyWindowVisible(this->mainWindow))
			this->mainWindow->updateMascots();
	});
	connect(this->MascotsAnimation, &mascotsAnimationsThread::updateMascotsAnimationSignal, this, [this] {
		if (this->mainWindow->ui.leftImg->isVisible() and utils::IsMyWindowVisible(this->mainWindow))
			this->mainWindow->flipMascots();
	});

	QTimer::singleShot(0, [this] {this->initTaskbar(); });

	connect(this->VW, &VideoWatcherQt::updateProgressBarSignal, this, [this](double position,double duration, QSharedPointer<BasePlayer> player,bool running) {
		this->mainWindow->updateProgressBar(position, duration, player,running); 
	});
	connect(this->VW, &VideoWatcherQt::updateTaskbarIconSignal, this, [this](bool watching) {
		this->mainWindow->setIsWatching(watching);
		this->mainWindow->updateIconByWatchingState();
	});
	connect(this->VW, &VideoWatcherQt::updateMusicPlayerSignal, this, [this](bool flag) {
		if (this->musicPlayer)
			if (flag)
				this->musicPlayer->unPause();
			else
				this->musicPlayer->pause();
		});
	connect(this->VW, &VideoWatcherQt::timeWatchedIncrementSignal, this, [this](double delta) {
		if (this->mainWindow) {
			this->mainWindow->incrementtimeWatchedIncrement(delta);
			this->mainWindow->checktimeWatchedIncrement();
			this->mainWindow->updateWatchedProgressBar();
		}
	});
	connect(this->VW, &VideoWatcherQt::sessionEndedSignal, this, [this](QString category, int videoCount, int completedCount, double watchTimeSec, double sessionTimeSec) {
		if (this->mainWindow && this->mainWindow->notificationManager) {
			this->mainWindow->notificationManager->showSessionSummary(category, videoCount, completedCount, watchTimeSec, sessionTimeSec);
		}
	});

	this->VW->start();

	std::string myappid = utils::getAppId(VERSION_TEXT);
	SetCurrentProcessExplicitAppUserModelID(std::wstring(myappid.begin(), myappid.end()).c_str());

	//connect(this,&MainApp::aboutToQuit,this,&MainApp::stop_handle);

	//numlock check and switch
	if (this->config->get_bool("numlock_only_on")) {
		numlock_only_on = true;
		utils::numlock_toggle_on();
	}
	else
		numlock_only_on = false;
	this->keyboard_hook = new RawInputKeyboard(this);

	this->quitEater = new QuitEater(this);
	this->installEventFilter(this->quitEater);
	this->genEventFilter = new generalEventFilter(this, this);
	if(this->config->get_bool("sound_effects_clicks"))
		this->installEventFilter(this->genEventFilter);
	this->tooltipFilter = new TooltipEventFilter(this);
	this->installEventFilter(this->tooltipFilter);
	this->mainWindow->init_icons();

	if (start_hidden) {
		//this is required for the scroll to work when window initially hidden
		this->mainWindow->setAttribute(Qt::WA_DontShowOnScreen, true);
		this->mainWindow->show();
		this->mainWindow->layout()->invalidate();
		this->mainWindow->hide();
		this->mainWindow->setAttribute(Qt::WA_DontShowOnScreen, false);
	}
	else
		this->mainWindow->show();
}

void MainApp::createMissingDirs() {
	QDir workingdir = QDir();
	workingdir.mkpath(ICONS_PATH);
	workingdir.mkpath(MASCOTS_PATH);
	workingdir.mkpath(MODELS_PATH);
	workingdir.mkpath(SOUND_EFFECTS_PATH);
	workingdir.mkpath(SOUND_EFFECTS_END_PATH);
	workingdir.mkpath(SOUND_EFFECTS_INTRO_PATH);
	workingdir.mkpath(UTILS_PATH);
}

void MainApp::toggleLogWindow() {
	if (this->logDialog == nullptr) {
		this->logDialog = new LogDialog();
		this->logDialog->setAttribute(Qt::WA_DeleteOnClose);
		this->logDialog->show();
	}
	else {
		utils::bring_hwnd_to_foreground_uiautomation_method((HWND)this->logDialog->winId(), qMainApp->uiAutomation);
	}
}

void MainApp::initTaskbar()
{
	this->hwnd = (HWND)this->mainWindow->winId();
	this->taskbar = new Taskbar(this->hwnd);
	this->taskbar->setPause(this->hwnd, true);
	this->mainWindow->updateProgressBar(this->mainWindow->position, this->mainWindow->duration);
}

void MainApp::startSingleInstanceServer(QString appid) {
	this->instanceServer = new QLocalServer;
	this->instanceServer->setSocketOptions(QLocalServer::WorldAccessOption);
	connect(this->instanceServer, &QLocalServer::newConnection, [this] {
		if (this->mainWindow) {
			this->mainWindow->iconActivated(QSystemTrayIcon::DoubleClick);
		}
	});
	if (!this->instanceServer->listen(appid)) {
		// A previous instance may have crashed, leaving a stale server name.
		// Remove it and retry once.
		QLocalServer::removeServer(appid);
		if (this->instanceServer->listen(appid)) {
			this->logger->log(QStringLiteral("Single instance server started (after cleanup) with id: %1").arg(appid), "INFO");
		} else {
			this->logger->log(QStringLiteral("Single instance server failed to start with id: %1").arg(appid), "ERROR");
		}
	} else {
		this->logger->log(QStringLiteral("Single instance server started with id: %1").arg(appid), "INFO");
	}
}

void MainApp::stopSingleInstanceServer() {
	if (this->instanceServer) {
		this->instanceServer->close();
		this->instanceServer->deleteLater();
		this->instanceServer = nullptr;
		this->logger->log("Single instance server stopped.", "INFO");
	}
}

void MainApp::showErrorMessage(QString message) {
	this->ErrorDialog->setWindowFlags(this->ErrorDialog->windowFlags() | Qt::WindowStaysOnTopHint);
	this->ErrorDialog->showMessage(message);
	this->logger->log(message, "Error");
}

// How long a single worker thread may take to stop before shutdown stops waiting on it.
// Generous for what the longest of them actually does on the way out (flushing the last
// watch rows, terminating a player process), short enough not to read as a hang.
static constexpr int SHUTDOWN_THREAD_WAIT_MS = 15000;
// Hard deadline for the whole shutdown, and the outer ceiling on the joins below. Without
// it a call with no timeout of its own (music/sound player stop, a window close handler)
// leaves a process with no windows, no way to quit, and no event loop left to run either
// exit-sound fallback. Set above one thread timeout so a single stuck thread still gets
// the per-thread trace rather than this one.
static constexpr int SHUTDOWN_WATCHDOG_MS = 45000;

// Set once stop_handle() got past its joins with every thread stopped. File-scope so the
// watchdog does not dereference the app while it is being torn down.
static std::atomic<bool> shutdown_finished{ false };

// Append to a file next to the working directory. The logger keeps its messages in
// memory, so nothing in it survives the kind of hang this traces: a shutdown that
// misfires is exactly when the record has to outlive the process.
static void writeShutdownTrace(const QString& message)
{
	QFile file(QDir::current().filePath(QStringLiteral("shutdown_trace.log")));
	if (file.open(QIODevice::Append | QIODevice::Text))
		file.write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ")) + message + QLatin1Char('\n')).toUtf8());
}

// Detached last resort. At this point the only thing still pending is the exit itself, so
// killing the process beats leaving one that cannot be closed.
static void armShutdownWatchdog()
{
	std::thread([] {
		std::this_thread::sleep_for(std::chrono::milliseconds(SHUTDOWN_WATCHDOG_MS));
		if (shutdown_finished.load())
			return;
		writeShutdownTrace(QStringLiteral("shutdown watchdog fired after %1 ms - forcing exit").arg(SHUTDOWN_WATCHDOG_MS));
		::TerminateProcess(::GetCurrentProcess(), 0);
	}).detach();
}

void MainApp::stop_handle()
{
	this->closeAllWindows();
	
	// Stop BPM manager first - this will cancel ongoing audio processing
	// and wait for thread pool to finish
	if (this->BpmManager) {
		this->BpmManager->stop();
	}
	
	// Each stop() also releases whatever can park its thread - the pixmap queue, the icon
	// and animation event locks - because a parked thread never re-reads its running
	// flag, which is what used to deadlock the joins at the end of this function.
	this->VW->running = false;
	this->mainWindow->animatedIcon->stop();
	this->MascotsGenerator->stop();
	this->MascotsAnimation->stop();
	this->mainWindow->animatedIcon->quit();
	this->VW->quit();
	this->MascotsGenerator->quit();
	this->MascotsAnimation->quit();
	
	if (this->musicPlayer) {
		this->musicPlayer->stop();
	}
	if (this->soundPlayer) {
		if (!this->exit_sound_called && this->soundPlayer->running && this->config->get_bool("sound_effects_exit")) {
			QMediaPlayer* s = this->soundPlayer->get_player();
			QString s_path = !this->soundPlayer->end_effects.isEmpty() ? *utils::select_randomly(this->soundPlayer->end_effects.begin(), this->soundPlayer->end_effects.end()) : "";
			//QSoundEffect* s = this->soundPlayer->get_player();
			if (s && !s_path.isEmpty()) {
				connect(s, &QMediaPlayer::mediaStatusChanged, [this, s](QMediaPlayer::MediaStatus status) {if (status == QMediaPlayer::EndOfMedia) { this->ready_to_quit = true; this->quit(); s->deleteLater(); } });
				connect(s, &QMediaPlayer::errorOccurred, [this, s](QMediaPlayer::Error error, const QString& errorString) {this->ready_to_quit = true; this->quit(); s->deleteLater(); });
				//connect(s, &QSoundEffect::playingChanged, [this, s] { if (!s->isPlaying()) { this->ready_to_quit = true; this->quit(); }});
				//connect(s, &QSoundEffect::statusChanged, [this, s] {if (s->status() == QSoundEffect::Error) { this->ready_to_quit = true; this->quit(); s->deleteLater(); }});
				s->setSource(QUrl::fromLocalFile(s_path));
				s->play();
				QTimer::singleShot(10000, [this] {this->ready_to_quit = true, this->quit(); });
			}
			else {
				this->ready_to_quit = true; 
				this->quit();
			}
			this->exit_sound_called = true;
			this->soundPlayer->stop();
		}
		else if(!this->exit_sound_called) {
			this->ready_to_quit = true;
			this->quit();
		}
		else {
			// Re-entered after the exit sound was already triggered: neither branch above
			// sets ready_to_quit, and QuitEater eats every Quit event while it is false.
			// Leaving it false here would strand the app with no windows and nothing left
			// that could ever set it again.
			this->ready_to_quit = true;
		}
	}
	else {
		this->ready_to_quit = true;
		this->quit();
	}
	// Armed before the joins: from here on a call with no timeout of its own must not be
	// able to leave a windowless process that cannot be quit.
	armShutdownWatchdog();

	this->stopSingleInstanceServer();
	// Bounded joins. A thread that will not stop used to hang the app for good, because
	// the Quit event was already eaten and both exit-sound fallbacks need the event loop
	// that these waits were blocking.
	const bool vwStopped = this->VW->wait(QDeadlineTimer(SHUTDOWN_THREAD_WAIT_MS));
	const bool iconStopped = this->mainWindow->animatedIcon->wait(QDeadlineTimer(SHUTDOWN_THREAD_WAIT_MS));
	// Wait for mascots threads to exit before deleteLater() destroys them -
	// deleting a running QThread (Qt 6.8 docs) results in a program crash.
	const bool generatorStopped = this->MascotsGenerator->wait(QDeadlineTimer(SHUTDOWN_THREAD_WAIT_MS));
	const bool animationStopped = this->MascotsAnimation->wait(QDeadlineTimer(SHUTDOWN_THREAD_WAIT_MS));

	if (!(vwStopped && iconStopped && generatorStopped && animationStopped)) {
		// The destructor would race a thread that is still touching this object, so
		// leave the process instead of tearing the app down around it.
		const QString stuck = (vwStopped ? QString() : QStringLiteral("VideoWatcherQt "))
			+ (iconStopped ? QString() : QStringLiteral("IconChanger "))
			+ (generatorStopped ? QString() : QStringLiteral("mascotsGenerator "))
			+ (animationStopped ? QString() : QStringLiteral("mascotsAnimations "));
		writeShutdownTrace(QStringLiteral("stop_handle: thread(s) did not stop within %1 ms (%2) - forcing exit")
			.arg(SHUTDOWN_THREAD_WAIT_MS).arg(stuck.trimmed()));
		::TerminateProcess(::GetCurrentProcess(), 0);
	}
	shutdown_finished.store(true);
}

MainApp::~MainApp()
{
	this->logger->log("MainApp destructor called. Cleaning up...", "INFO");
	timeEndPeriod(2);
	if(this->mainWindow)
		this->mainWindow->deleteLater();
	delete this->config;
	delete this->db;
	delete this->shoko_API;
	delete this->taskbar;
	delete this->musicPlayer;
	delete this->soundPlayer;
	if(this->quitEater)
		this->quitEater->deleteLater();
	delete this->keyboard_hook;
	this->VW->deleteLater();
	this->MascotsAnimation->deleteLater();
	this->MascotsGenerator->deleteLater();
	if (this->BpmManager)
		this->BpmManager->deleteLater();
	this->logger->log("MainApp cleanup finished.", "INFO");
	this->logger->deleteLater();
	this->ErrorDialog->deleteLater();
}
