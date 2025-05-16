#include "stdafx.h"
#include "ProgressBarQLabel.h"
#include "MainWindow.h"
#include "utils.h"
#include <QLabel>
#include <QEvent>
#include "MainApp.h"
#include <QStyleOptionProgressBar>

ProgressBarQLabel::ProgressBarQLabel(QWidget* parent) : customQLabel(parent) {
	this->w = 1.0 / 25;
	this->scaled_mode = true;
	this->setBrush(this->palette().color(QPalette::WindowText));
	this->setPen(this->palette().color(QPalette::Window));
}

void ProgressBarQLabel::copy(ProgressBarQLabel* other) {
	this->setMinMax(other->minimum(), other->maximum());
	this->setProgress(other->progress());
	this->vertical_orientation = other->vertical_orientation;
	this->w = other->w;
	this->scaled_mode = other->scaled_mode;
	this->highlight_mode = other->highlight_mode;
	this->setBrush(other->brush());
	this->setPen(other->pen());
	this->setFont(other->font());
	this->setText(other->text());
	this->setAlignment(other->alignment());
}

bool ProgressBarQLabel::scaledOutlineMode() {
	return this->scaled_mode;
}
void ProgressBarQLabel::setScaledOutlineMode(bool state) {
	this->scaled_mode = state;
}
double ProgressBarQLabel::outlineThickness() const {
	if (this->scaled_mode)
		return this->w * this->font().pointSize();
	else
		return this->w;
}
double ProgressBarQLabel::outlineThickness(QFont &f) const {
	if (this->scaled_mode)
		return this->w * f.pointSize();
	else
		return this->w;
}
void ProgressBarQLabel::setOutlineThickness(double value) {
	this->w = value;
}
void ProgressBarQLabel::setBrush(QBrush brush) {
	this->brush_ = brush;
}
void ProgressBarQLabel::setBrush(QColor color) {
	this->brush_ = QBrush(color);
}
void ProgressBarQLabel::setBrush(Qt::GlobalColor color) {
	this->brush_ = QBrush(color);
}
void ProgressBarQLabel::setPen(QPen pen) {
	this->pen_ = pen;
}
void ProgressBarQLabel::setPen(QColor color) {
	this->pen_ = QPen(color);
}
void ProgressBarQLabel::setPen(Qt::GlobalColor color) {
	this->pen_ = QPen(color);
}
void ProgressBarQLabel::setProgress(int progress, bool update)
{
	this->progress_ = progress;
	if (highlight_check()) {
		QFont font_ = this->font();
		font_.setPointSizeF(24);
		this->setFont(font_);
	}
	else {
		QFont font_ = this->font();
		font_.setPointSizeF(16);
		this->setFont(font_);
	}
	if(update)
		this->update();
}
void ProgressBarQLabel::setMinMax(int minimum, int maximum, bool update)
{
	this->minimum_ = minimum;
	this->maximum_ = maximum;
	if (highlight_check()) {
		QFont font_ = this->font();
		font_.setPointSizeF(24);
		this->setFont(font_);
	}
	else {
		QFont font_ = this->font();
		font_.setPointSizeF(16);
		this->setFont(font_);
	}
	if(update)
		this->update();
}
int& ProgressBarQLabel::progress()
{
	return this->progress_;
}
int& ProgressBarQLabel::minimum()
{
	return this->minimum_;
}
int& ProgressBarQLabel::maximum()
{
	return this->maximum_;
}
QBrush& ProgressBarQLabel::brush()
{
	return this->brush_;
}
QPen& ProgressBarQLabel::pen()
{
	return this->pen_;
}
bool ProgressBarQLabel::highlight_check() {
	if (this->highlight_mode && this->progress() >= this->maximum())
		return true;
	else
		return false;
}
QSize ProgressBarQLabel::sizeHint() const
{
	double w = std::ceil(this->outlineThickness() * 2);
	return QLabel::sizeHint().grownBy(QMargins(3, 0, 3, 0)) + QSize(w, w);
}
QSize ProgressBarQLabel::minimumSizeHint() const
{
	double w = std::ceil(this->outlineThickness() * 2);
	return QLabel::minimumSizeHint().grownBy(QMargins(3, 0, 3, 0)) + QSize(w, w);
}
void ProgressBarQLabel::paintEvent(QPaintEvent* e)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QRect rect = this->rect();

	QStyleOptionProgressBar progress_bar_option = QStyleOptionProgressBar();
	if(vertical_orientation)
		progress_bar_option.state = QStyle::StateFlag::State_Enabled;
	else
		progress_bar_option.state = QStyle::StateFlag::State_Horizontal;
	progress_bar_option.minimum = this->minimum();
	progress_bar_option.maximum = this->maximum();
	progress_bar_option.progress = this->progress();
	progress_bar_option.rect = rect;
	QStyle* style = QApplication::style();
	QColor highlight_color = this->palette().color(QPalette::Highlight);
	
	int indent;
	double x, y;
	QFont font_ = this->font();
	QBrush brush = this->brush_;

	if (highlight_check()) {
		progress_bar_option.palette.setColor(QPalette::Highlight, utils::get_vibrant_color_exponential(highlight_color,0.12,0.12));
		font_.setPointSizeF(24);
	}
	else {
		font_.setPointSizeF(16);
	}
	style->drawControl(QStyle::CE_ProgressBar, &progress_bar_option, &painter);

	//https://stackoverflow.com/questions/64290561/qlabel-correct-positioning-for-text-outline
	double w = this->outlineThickness(font_);

	QFontMetrics metrics = QFontMetrics(font_);
	QRect tr = metrics.boundingRect(this->text()).adjusted(0, 0, w, w);
	if (this->indent() == -1)
		if (this->frameWidth())
			indent = (metrics.boundingRect('x').width() + w * 2) / 2;
		else
			indent = w;
	else
		indent = this->indent();

	if (this->alignment() & Qt::AlignLeft)
		x = rect.left() + indent - std::min(metrics.leftBearing(this->text()[0]), 0);
	else if (this->alignment() & Qt::AlignRight)
		x = rect.x() + rect.width() - indent - tr.width();
	else
		x = (rect.width() - tr.width()) / 2.0;

	if (this->alignment() & Qt::AlignTop)
		y = rect.top() + indent + metrics.ascent();
	else if (this->alignment() & Qt::AlignBottom)
		y = rect.y() + rect.height() - indent - metrics.descent();
	else
		y = (rect.height() + metrics.ascent() - metrics.descent()) / 2.0;

	QPainterPath path = QPainterPath();
	path.addText(x, y, font_, this->text());
	this->pen_.setWidthF(w * 2);
	painter.strokePath(path, this->pen_);
	if (1 < brush.style() and brush.style() < 15)
		painter.fillPath(path, this->palette().window());
	painter.fillPath(path, brush);
}
ProgressBarQLabel::~ProgressBarQLabel() {

}