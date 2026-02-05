#include "stdafx.h"
#include "NextChoiceDialog.h"
#include <QFileInfo>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QMouseEvent>
#include <QEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QDialogButtonBox>
#include <algorithm>
#include <QWheelEvent>
#include <QScrollBar>
#include <QPainter>
#include <QSize>
#include "starEditorWidget.h"
#include "utils.h"
#include "MainApp.h"
#include "VideoPreviewWidget.h"

namespace {
class ChoiceCardWidget : public QWidget {
public:
    explicit ChoiceCardWidget(int index, QWidget* parent = nullptr) : QWidget(parent) {
        this->setAttribute(Qt::WA_Hover, true);
        this->setMouseTracking(true);
        this->setProperty("choiceIndex", index);
        this->setCursor(Qt::PointingHandCursor);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }
protected:
    bool event(QEvent* e) override {
        switch (e->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
            this->hovered = true;
            this->update();
            break;
        case QEvent::Leave:
        case QEvent::HoverLeave:
            this->hovered = false;
            this->update();
            break;
        case QEvent::PaletteChange:
            this->update();
            break;
        default:
            break;
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QColor background = this->palette().color(QPalette::Base);
        if (background.isValid()) {
            background = background.darker(115);
        } else {
            background = QColor(43, 43, 43);
        }

        QColor border = this->hovered ? this->palette().color(QPalette::Highlight) : this->palette().color(QPalette::Mid);
        if (!border.isValid()) {
            border = this->hovered ? QColor(90, 160, 255) : QColor(85, 85, 85);
        }

        QRectF rect = this->rect();
        rect.adjust(0.5, 0.5, -0.5, -0.5);

        painter.setBrush(background);
        painter.setPen(QPen(border, 1.0));
        painter.drawRoundedRect(rect, 8.0, 8.0);
    }
private:
    bool hovered = false;
};

class HorizontalScrollArea : public QScrollArea {
public:
    explicit HorizontalScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {
        this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        this->setWidgetResizable(false);
        this->setFrameShape(QFrame::NoFrame);
        this->setFocusPolicy(Qt::NoFocus);
        this->setStyleSheet("QScrollArea { background: transparent; } QWidget { background: transparent; }");
    }
protected:
    void wheelEvent(QWheelEvent* e) override {
        int delta = e->angleDelta().y();
        if (delta == 0) delta = e->angleDelta().x();
        if (QScrollBar* bar = this->horizontalScrollBar()) {
            bar->setValue(bar->value() - delta);
        }
        e->accept(); // prevent parent scroll area from scrolling
    }
};
}

NextChoiceDialog::NextChoiceDialog(QWidget* parent) : QDialog(parent)
{
    this->ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    this->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    connect(this->ui.refreshButton, &QPushButton::clicked, this, [this] {
        emit this->refreshRequested();
    });
    connect(this->ui.randomButton, &QPushButton::clicked, this, [this] {
        if (!this->choices.isEmpty()) {
            int index = utils::randint(0, this->choices.size() - 1);
            this->applySelection(index);
            this->accept();
        }
    });
    if (auto buttonBox = this->findChild<QDialogButtonBox*>()) {
        connect(buttonBox, &QDialogButtonBox::rejected, this, &NextChoiceDialog::reject);
    }
    this->bringToFrontTimer.setInterval(250);
    connect(&this->bringToFrontTimer, &QTimer::timeout, this, [this] {
        if (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
            return;
        utils::bring_hwnd_to_foreground_uiautomation_method((HWND)this->winId(), qMainApp->uiAutomation);
        this->raise();
        this->show();
        this->activateWindow();
        });
    this->bringToFrontTimer.start();
    connect(this, &QDialog::finished, this, [this](int) {
        this->bringToFrontTimer.stop();
        });
}

NextChoiceDialog::~NextChoiceDialog()
{
    this->stopAllPreviews();
}

void NextChoiceDialog::setChoices(const QList<NextVideoChoice>& newChoices)
{
    this->choices = newChoices;
    this->previewAutoplayAllMute = qMainApp->config->get_bool("preview_autoplay_all_mute");
    this->previewEnabled = qMainApp->config->get_bool("preview_next_choices_enabled");
    this->updateRefreshButtonText();
    this->rebuildCards();
    if (!this->choices.isEmpty()) {
        this->applySelection(0);
    }

    // Dynamically size the dialog to fit cards while respecting screen bounds
    const int availableChoices = this->choices.size() > 0 ? this->choices.size() : 1;
    int columns = availableChoices;
    if (columns < 1) columns = 1;
    if (columns > 3) columns = 3;
    int rows = this->choices.isEmpty() ? 1 : ((this->choices.size() + columns - 1) / columns);

    int screenWidth = 1280;
    int screenHeight = 720;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect avail = screen->availableGeometry();
        screenWidth = avail.width();
        screenHeight = avail.height();
    }

    const int cardMaxWidth = screenWidth / 4; // aim for ~1/4 screen per card
    const int cardWidth = cardMaxWidth;
    const int baseCardHeight = 210; // closer to real rendered height including tags/stats

    int cardHeight = baseCardHeight;
    if (this->previewEnabled) {
        const double previewWidthFraction = 0.55; // preview roughly half the card width
        const int nonPreviewHeightEstimate = 80; // title + stats + padding
        QSize adjusted = utils::adjustSizeForAspect(QSize(cardWidth, baseCardHeight), previewWidthFraction, nonPreviewHeightEstimate, { 16.0 / 9.0, 4.0 / 3.0, 21.0 / 9.0 });
        cardHeight = adjusted.height();
    }

    int hSpacing = 12;
    int vSpacing = 12;
    if (auto* grid = qobject_cast<QGridLayout*>(this->ui.cardsLayout)) {
        hSpacing = grid->horizontalSpacing();
        vSpacing = grid->verticalSpacing();
    }

    int desiredWidth = columns * cardWidth + (columns - 1) * hSpacing + 40;
    int desiredHeight = rows * cardHeight + (rows - 1) * vSpacing + 80;

    if (desiredWidth > screenWidth - 40) desiredWidth = screenWidth - 40;
    if (desiredHeight > screenHeight - 40) desiredHeight = screenHeight - 40;

    this->resize(desiredWidth, desiredHeight);
}

NextVideoChoice NextChoiceDialog::selectedChoice() const
{
    if (this->currentIndex >= 0 && this->currentIndex < this->choices.size()) {
        return this->choices[this->currentIndex];
    }
    return NextVideoChoice();
}

void NextChoiceDialog::setCounterLabel(QLabel* label)
{
    this->counterLabel = label;
    this->updateRefreshButtonText();
}

void NextChoiceDialog::updateRefreshButtonText()
{
    int counterVar = 0;
    if (this->counterLabel) {
        bool ok = false;
        int value = this->counterLabel->text().toInt(&ok);
        if (ok) {
            counterVar = value;
        }
    }
    this->ui.refreshButton->setText(QStringLiteral("Refresh (%1)").arg(counterVar));
}

QWidget* NextChoiceDialog::buildCard(const NextVideoChoice& choice, int index)
{
    QWidget* card = new ChoiceCardWidget(index, this);
    card->setObjectName(QStringLiteral("choiceCard_%1").arg(index));

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    QString title = choice.name.isEmpty() ? QFileInfo(choice.path).completeBaseName() : choice.name;
    QString authorText = choice.author.isEmpty() ? QStringLiteral("Unknown") : choice.author;

    auto makeScrollableLabel = [&](const QString& text, const QString& style, int fixedHeight) -> QWidget* {
        HorizontalScrollArea* area = new HorizontalScrollArea(card);
        QLabel* lbl = new QLabel(text, area);
        lbl->setStyleSheet(style);
        lbl->setWordWrap(false);
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        area->setWidget(lbl);
        area->setFixedHeight(fixedHeight);
        return area;
    };

    QWidget* titleWidget = makeScrollableLabel(QStringLiteral("%1").arg(title), "font-size: 15px; font-weight: 700;", 22);
    layout->addWidget(titleWidget);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    auto* infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    QLabel* authorLabel = new QLabel(QStringLiteral("%1").arg(authorText), card);
    authorLabel->setStyleSheet("color: #c0c0c0; font-size: 12px;");
    infoLayout->addWidget(authorLabel);

    if (!choice.tags.isEmpty()) {
        QLabel* tagsLabel = new QLabel(choice.tags, card);
        tagsLabel->setWordWrap(true);
        tagsLabel->setStyleSheet("color: #8aa; font-size: 11px;");
        infoLayout->addWidget(tagsLabel);
    }

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);

    auto addStat = [&](const QString& label, const QString& value, int row) {
        QLabel* l = new QLabel(label, card);
        l->setStyleSheet("color: #a0a0a0; font-size: 11px;");
        QLabel* v = new QLabel(value, card);
        v->setStyleSheet("font-weight: 600; font-size: 11px;");
        grid->addWidget(l, row, 0);
        grid->addWidget(v, row, 1);
    };

    addStat(QStringLiteral("Type"), choice.type, 0);
    addStat(QStringLiteral("BPM"), choice.bpm > 0 ? QString::number(qRound(choice.bpm)) : QStringLiteral("-"), 1);
    addStat(QStringLiteral("Views"), QString::number(choice.views), 2);

    if (choice.rating > 0.0 && this->activeIcon && this->inactiveIcon) {
        QWidget* ratingContainer = new QWidget(card);
        QHBoxLayout* ratingLayout = new QHBoxLayout(ratingContainer);
        ratingLayout->setContentsMargins(0, 0, 0, 0);
        ratingLayout->setSpacing(4);

        starEditorWidget* stars = new starEditorWidget(ratingContainer);
        stars->setEditMode(starEditorWidget::EditMode::NoEdit);
        stars->setStarPixelSize(16);
        StarRating sr(this->activeIcon, this->halfIcon, this->inactiveIcon, choice.rating, 5.0);
        sr.setStarPixelSize(16);
        stars->setStarRating(sr);
        stars->setFocusPolicy(Qt::NoFocus);

        QLabel* ratingLabel = new QLabel(QString::number(choice.rating, 'f', 1), ratingContainer);
        ratingLabel->setStyleSheet("color: #c0c0c0; font-size: 11px;");

        ratingLayout->addWidget(stars, 0, Qt::AlignLeft | Qt::AlignVCenter);
        ratingLayout->addWidget(ratingLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
        ratingLayout->addStretch(1);

        grid->addWidget(new QLabel(QStringLiteral("Rating"), card), 3, 0);
        grid->addWidget(ratingContainer, 3, 1);
    } else {
        addStat(QStringLiteral("Rating"), QStringLiteral("-"), 3);
    }

    addStat(QStringLiteral("Last W"), choice.lastWatched.isEmpty() ? QStringLiteral("-") : choice.lastWatched, 4);

    infoLayout->addLayout(grid);

    if (choice.probability >= 0.0) {
        QLabel* prob = new QLabel(QStringLiteral("Chance: %1%").arg(QString::number(choice.probability, 'f', 2)), card);
        prob->setStyleSheet("color: #80d080; font-weight: 700;");
        infoLayout->addWidget(prob);
    }
    infoLayout->addStretch(1);

    contentLayout->addLayout(infoLayout, 1);

    if (this->previewEnabled) {
        VideoPreviewWidget* preview = new VideoPreviewWidget(card);
        preview->setSource(choice.path);
        preview->setStartDelay(0);
        preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        int previewVolume = qBound(0, qMainApp->config->get("preview_volume").toInt(), 100);
        preview->setVolume(previewVolume);
        bool randomHover = qMainApp->config->get_bool("preview_random_each_hover") && !this->previewAutoplayAllMute;
        preview->setRandomStartEnabled(qMainApp->config->get_bool("preview_random_start"));
        preview->setRememberPositionEnabled(qMainApp->config->get_bool("preview_remember_position"));
        preview->setRandomOnHoverEnabled(randomHover);
        bool seeded = qMainApp->config->get_bool("preview_seeded_random") && qMainApp->config->get_bool("random_use_seed");
        preview->setSeededRandom(seeded, qMainApp->config->get("random_seed"));
        double overlayScale = std::max(0.1, qMainApp->config->get("preview_overlay_scale").toDouble());
        int overlayPadX = std::max(0, qMainApp->config->get("preview_overlay_pad_x").toInt());
        int overlayPadY = std::max(0, qMainApp->config->get("preview_overlay_pad_y").toInt());
        int overlayMargin = std::max(0, qMainApp->config->get("preview_overlay_margin").toInt());
        preview->setOverlayStyle(overlayScale, overlayPadX, overlayPadY, overlayMargin);
        preview->setMuted(true);
        preview->prepareInitialFrame(this->previewAutoplayAllMute);
        contentLayout->addWidget(preview, 1);
        contentLayout->setStretch(0, 1);
        contentLayout->setStretch(1, 1);
        this->previewWidgets.insert(index, preview);
    } else {
        contentLayout->addStretch(1);
        contentLayout->setStretch(0, 1);
    }

    layout->addLayout(contentLayout);

    QWidget* pathWidget = makeScrollableLabel(choice.path, "color: #999; font-size: 10px;", 18);
    layout->addWidget(pathWidget);

    card->setProperty("choiceIndex", index);
    card->installEventFilter(this);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        int maxWidth = std::max(220, screen->availableGeometry().width() / 4);
        card->setMaximumWidth(maxWidth);
    }
    return card;
}

void NextChoiceDialog::startPreviewForIndex(int index)
{
    if (!this->previewWidgets.contains(index))
        return;
    for (auto it = this->previewWidgets.constBegin(); it != this->previewWidgets.constEnd(); ++it) {
        if (!it.value())
            continue;
        if (it.key() == index) {
            it.value()->setMuted(false);
            if (this->previewAutoplayAllMute) {
                if (!it.value()->isPlaying()) {
                    it.value()->startPreview();
                }
            } else {
                it.value()->startPreview();
            }
        } else if (this->previewAutoplayAllMute) {
            it.value()->setMuted(true);
            // keep playing; no restart to preserve position
        } else {
            it.value()->setMuted(true);
            it.value()->stopPreview();
        }
    }
}

void NextChoiceDialog::stopPreviewForIndex(int index)
{
    if (auto* preview = this->previewWidgets.value(index)) {
        preview->setMuted(true);
        if (!this->previewAutoplayAllMute) {
            preview->stopPreview();
        }
    }
}

void NextChoiceDialog::stopAllPreviews()
{
    for (auto* preview : this->previewWidgets) {
        if (preview) {
            preview->stopPreview(true);
        }
    }
}


void NextChoiceDialog::applySelection(int index)
{
    this->currentIndex = index;
}

void NextChoiceDialog::rebuildCards()
{
    QGridLayout* grid = qobject_cast<QGridLayout*>(this->ui.cardsLayout);
    if (!grid) return;
    this->stopAllPreviews();
    this->previewWidgets.clear();
    while (QLayoutItem* item = grid->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const int maxColumns = 3;
    int row = 0;
    int col = 0;
    for (int i = 0; i < this->choices.size(); ++i) {
        QWidget* card = this->buildCard(this->choices[i], i);
        grid->addWidget(card, row, col);
        col++;
        if (col >= maxColumns) {
            col = 0;
            row++;
        }
    }
    int usedColumns = this->choices.isEmpty() ? 1 : this->choices.size();
    if (usedColumns < 1) usedColumns = 1;
    if (usedColumns > maxColumns) usedColumns = maxColumns;
    for (int c = 0; c < maxColumns; ++c) {
        grid->setColumnStretch(c, c < usedColumns ? 1 : 0);
    }
}

bool NextChoiceDialog::eventFilter(QObject* obj, QEvent* event)
{
    auto findChoiceIndex = [obj]() -> int {
        QObject* cur = obj;
        QVariant prop;
        while (cur && !(prop = cur->property("choiceIndex")).isValid()) {
            cur = cur->parent();
        }
        bool ok = false;
        int idx = prop.toInt(&ok);
        return ok ? idx : -1;
    };

    switch (event->type()) {
    case QEvent::MouseButtonRelease: {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        int idx = findChoiceIndex();
        if (idx >= 0 && idx < this->choices.size()) {
            if (mouseEvent->button() == Qt::LeftButton) {
                this->applySelection(idx);
                this->accept();
            } else if (mouseEvent->button() == Qt::RightButton || mouseEvent->button() == Qt::MiddleButton) {
                this->startPreviewForIndex(idx);
                if (auto* preview = this->previewWidgets.value(idx)) {
                    preview->jumpToRandomPosition();
                }
            }
            return true;
        }
        break;
    }
    case QEvent::Wheel: {
        QObject* curObj = obj;
        // Ignore wheel events coming from scrollable labels/areas
        if (curObj && curObj->inherits("QAbstractScrollArea")) {
            break;
        }
        QMouseEvent* dummyMouse = nullptr;
        int idx = findChoiceIndex();
        if (idx >= 0 && idx < this->choices.size()) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            int delta = wheel->angleDelta().y();
            if (delta == 0)
                delta = wheel->angleDelta().x();
            if (delta == 0)
                break;
            double seconds = qMainApp->config->get("preview_seek_seconds").toDouble();
            if (seconds <= 0.0)
                break;
            if (auto* preview = this->previewWidgets.value(idx)) {
                double dir = delta > 0 ? 1.0 : -1.0;
                preview->seekBySeconds(dir * seconds);
                return true;
            }
        }
        break;
    }
    case QEvent::Enter:
    case QEvent::HoverEnter: {
        int idx = findChoiceIndex();
        if (idx >= 0 && idx < this->choices.size()) {
            this->startPreviewForIndex(idx);
        }
        break;
    }
    case QEvent::Leave:
    case QEvent::HoverLeave: {
        int idx = findChoiceIndex();
        if (idx >= 0 && idx < this->choices.size()) {
            this->stopPreviewForIndex(idx);
        }
        break;
    }
    default:
        break;
    }
    return QDialog::eventFilter(obj, event);
}


void NextChoiceDialog::closeEvent(QCloseEvent* e)
{
    this->stopAllPreviews();
    QDialog::closeEvent(e);
}
