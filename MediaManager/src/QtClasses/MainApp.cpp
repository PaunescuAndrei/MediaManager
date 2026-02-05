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
#include "timeapi.h"
#include "generalEventFilter.h"
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
		this->mainWindow->setIconWatchingState(watching);
		this->mainWindow->updateIconByWatchingState();
	});
	connect(this->VW, &VideoWatcherQt::updateMusicPlayerSignal, this, [this](bool flag) {
		if (this->musicPlayer)
			if (flag)
				this->musicPlayer->unPause();
			else
				this->musicPlayer->pause();
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
	if(this->instanceServer->listen(appid)){
		this->logger->log(QStringLiteral("Single instance server started with id: %1").arg(appid), "INFO");
	} else {
		this->logger->log(QStringLiteral("Single instance server failed to start with id: %1").arg(appid), "ERROR");
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

void MainApp::stop_handle()
{
	this->closeAllWindows();
	this->VW->running = false;
	this->mainWindow->animatedIcon->running = false;
	this->MascotsGenerator->running = false;
	this->MascotsAnimation->running = false;
	this->MascotsAnimation->runningEvent.set();
	this->mainWindow->animatedIcon->animatedIconEvent.set();
	this->mainWindow->animatedIcon->setIcon_lock.set();
	this->mainWindow->animatedIcon->quit();
	this->VW->quit();
	this->MascotsGenerator->quit();
	this->MascotsAnimation->quit();
	if (this->BpmManager) {
		this->BpmManager->stop();
	}
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
	}
	else {
		this->ready_to_quit = true;
		this->quit();
	}
	this->stopSingleInstanceServer();
	this->VW->wait();
	this->mainWindow->animatedIcon->wait();
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
