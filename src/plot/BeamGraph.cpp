#include "BeamGraph.h"

#include "qcp/src/core.h"
#include "qcp/src/painter.h"

#include <QSpinBox>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QToolButton>

//------------------------------------------------------------------------------
//                               BeamColorMapData
//------------------------------------------------------------------------------

BeamColorMap::BeamColorMap(QCPAxis *keyAxis, QCPAxis *valueAxis) : QCPColorMap(keyAxis, valueAxis)
{
}

void BeamColorMap::setGradient(const QCPColorGradient &gradient)
{
    // Base setGradient() checks for gradient equality and skips if they are same.
    // PrecalculatedGradient always gets equal to another one even when they colors differ.
    // So redeclare setGradient() to get rid of equality check
    mGradient = gradient;
    mMapImageInvalidated = true;
    emit gradientChanged(mGradient);
}

//------------------------------------------------------------------------------
//                               BeamColorMapData
//------------------------------------------------------------------------------

BeamColorMapData::BeamColorMapData(int w, int h)
    : QCPColorMapData(w, h, QCPRange(0, w), QCPRange(0, h))
{
}

//------------------------------------------------------------------------------
//                               BeamColorScale
//------------------------------------------------------------------------------

BeamColorScale::BeamColorScale(QCustomPlot *parentPlot) : QCPColorScale(parentPlot)
{
    setRangeDrag(false);
    setRangeZoom(false);
}

void BeamColorScale::setFrameColor(const QColor& c)
{
    for (auto a : mAxisRect->axes())
        a->setBasePen(QPen(c, 0, Qt::SolidLine));
}

//------------------------------------------------------------------------------
//                                BeamEllipse
//------------------------------------------------------------------------------

BeamEllipse::BeamEllipse(QCustomPlot *parentPlot) : QCPAbstractItem(parentPlot)
{
}

void BeamEllipse::draw(QCPPainter *painter)
{
    if (!qIsFinite(xc) || !qIsFinite(yc) || !qIsFinite(dx) || !qIsFinite(dy)) return;
    const double x = parentPlot()->xAxis->coordToPixel(xc);
    const double y = parentPlot()->yAxis->coordToPixel(yc);
    const double rx = parentPlot()->xAxis->coordToPixel(xc + dx/2.0) - x;
    const double ry = parentPlot()->yAxis->coordToPixel(yc + dy/2.0) - y;
    auto t = painter->transform();
    painter->translate(x, y);
    painter->rotate(phi);
    painter->setPen(pen);
    painter->drawEllipse(QPointF(), rx, ry);
    painter->setTransform(t);
}

//------------------------------------------------------------------------------
//                               ApertureRect
//------------------------------------------------------------------------------

RoiRectGraph::RoiRectGraph(QCustomPlot *parentPlot) : QCPAbstractItem(parentPlot)
{
    _pen = QPen(Qt::yellow, 0, Qt::DashLine);
    _editPen = QPen(Qt::yellow, 3, Qt::SolidLine);

    connect(parentPlot, &QCustomPlot::mouseMove, this, &RoiRectGraph::mouseMove);
    connect(parentPlot, &QCustomPlot::mousePress, this, &RoiRectGraph::mousePress);
    connect(parentPlot, &QCustomPlot::mouseRelease, this, &RoiRectGraph::mouseRelease);
    connect(parentPlot, &QCustomPlot::mouseDoubleClick, this, &RoiRectGraph::mouseDoubleClick);
}

void RoiRectGraph::setRoi(const RoiRect &roi)
{
    _roi = roi;
    updateCoords();
    updateVisibility();
}

void RoiRectGraph::setIsVisible(bool on)
{
    _isVisible = on;
    updateVisibility();
}

void RoiRectGraph::setImageSize(int sensorW, int sensorH, const PixelScale &scale)
{
    _scale = scale;
    _maxX = scale.sensorToUnit(sensorW);
    _maxY = scale.sensorToUnit(sensorH);
    updateCoords();
}

void RoiRectGraph::updateVisibility()
{
    setVisible(_editing || (_roi.on && _isVisible));
}

void RoiRectGraph::updateCoords()
{
    _x1 = _scale.sensorToUnit(_roi.x1);
    _y1 = _scale.sensorToUnit(_roi.y1);
    _x2 = _scale.sensorToUnit(_roi.x2);
    _y2 = _scale.sensorToUnit(_roi.y2);
}

void RoiRectGraph::startEdit()
{
    if (_editing)
        return;
    _editing = true;
    makeEditor();
    updateVisibility();
    parentPlot()->replot();
}

void RoiRectGraph::stopEdit(bool apply)
{
    if (!_editing)
        return;
    _editing = false;
    if (apply) {
        _roi.x1 = _scale.unitToSensor(_x1);
        _roi.y1 = _scale.unitToSensor(_y1);
        _roi.x2 = _scale.unitToSensor(_x2);
        _roi.y2 = _scale.unitToSensor(_y2);
        _roi.on = true;
        if (onEdited)
            onEdited();
    } else {
        updateCoords();
    }
    updateVisibility();
    _editor->deleteLater();
    _editor = nullptr;
    QToolTip::hideText();
    resetDragCusrsor();
    parentPlot()->replot();
}

static const int dragThreshold = 10;

void RoiRectGraph::draw(QCPPainter *painter)
{
    const double x1 = parentPlot()->xAxis->coordToPixel(_x1);
    const double y1 = parentPlot()->yAxis->coordToPixel(_y1);
    const double x2 = parentPlot()->xAxis->coordToPixel(_x2);
    const double y2 = parentPlot()->yAxis->coordToPixel(_y2);
    if (_editing) {
        auto r = parentPlot()->axisRect()->rect();
        painter->setPen(_pen);
        painter->drawLine(QLineF(r.left(), y1, r.right(), y1));
        painter->drawLine(QLineF(r.left(), y2, r.right(), y2));
        painter->drawLine(QLineF(x1, r.top(), x1, r.bottom()));
        painter->drawLine(QLineF(x2, r.top(), x2, r.bottom()));
        painter->setPen(_editPen);
        painter->drawRect(x1, y1, x2-x1, y2-y1);
    } else {
        painter->setPen(_pen);
        painter->drawRect(x1, y1, x2-x1, y2-y1);
    }
//    painter->setPen(_editing ? _editPen : _pen);
//    painter->drawRect(x1, y1, x2-x1, y2-y1);
}

void RoiRectGraph::mouseMove(QMouseEvent *e)
{
    if (!visible() || !_editing) return;

    if (!parentPlot()->axisRect()->rect().contains(e->pos())) {
        resetDragCusrsor();
        _dragging = false;
        return;
    }

    const double x = e->pos().x();
    const double y = e->pos().y();
    const double t = dragThreshold;
    const double x1 = parentPlot()->xAxis->coordToPixel(_x1);
    const double y1 = parentPlot()->yAxis->coordToPixel(_y1);
    const double x2 = parentPlot()->xAxis->coordToPixel(_x2);
    const double y2 = parentPlot()->yAxis->coordToPixel(_y2);

    if (_dragging) {
        const int dx = x - _dragX;
        const int dy = y - _dragY;
        _dragX = x;
        _dragY = y;
        if (_drag0 || _dragX1) {
            _x1 = parentPlot()->xAxis->pixelToCoord(x1+dx);
            _x1 = qMax(_x1, 0.0);
            _seX1->setValue(_x1);
            _seW->setValue(roiW());
        }
        if (_drag0 || _dragX2) {
            _x2 = parentPlot()->xAxis->pixelToCoord(x2+dx);
            _x2 = qMin(_x2, _maxX);
            _seX2->setValue(_x2);
            _seW->setValue(roiW());
        }
        if (_drag0 || _dragY1) {
            _y1 = parentPlot()->yAxis->pixelToCoord(y1+dy);
            _y1 = qMax(_y1, 0.0);
            _seY1->setValue(_y1);
            _seH->setValue(roiH());
        }
        if (_drag0 || _dragY2) {
            _y2 = parentPlot()->yAxis->pixelToCoord(y2+dy);
            _y2 = qMin(_y2, _maxY);
            _seY2->setValue(_y2);
            _seH->setValue(roiH());
        }
        showCoordTooltip(e->globalPosition().toPoint());
        parentPlot()->replot();
        return;
    }

    _drag0 = x > x1+t && x < x2-t && y > y1+t && y < y2-t;
    _dragX1 = !_drag0 && qAbs(x1-x) < t;
    _dragX2 = !_drag0 && qAbs(x2-x) < t;
    _dragY1 = !_drag0 && qAbs(y1-y) < t;
    _dragY2 = !_drag0 && qAbs(y2-y) < t;
//    _dragX1 = qAbs(x1-x) < t && y >= (y1-t) && y <= (y2+t);
//    _dragX2 = qAbs(x2-x) < t && y >= (y1-t) && y <= (y2+t);
//    _dragY1 = qAbs(y1-y) < t && x >= (x1-t) && x <= (x2+t);
//    _dragY2 = qAbs(y2-y) < t && x >= (x1-t) && x <= (x2+t);

    if (_drag0)
        showDragCursor(Qt::SizeAllCursor);
    else if ((_dragX1 && _dragY1) || (_dragX2 && _dragY2))
        showDragCursor(Qt::SizeFDiagCursor);
    else if ((_dragX1 && _dragY2) || (_dragX2 && _dragY1))
        showDragCursor(Qt::SizeBDiagCursor);
    else if (_dragX1 || _dragX2)
        showDragCursor(Qt::SizeHorCursor);
    else if (_dragY1 || _dragY2)
        showDragCursor(Qt::SizeVerCursor);
    else
        resetDragCusrsor();
}

void RoiRectGraph::mousePress(QMouseEvent *e)
{
    if (!visible() || !_editing) return;
    if (e->button() != Qt::LeftButton) return;
    _dragging = true;
    _dragX = e->pos().x();
    _dragY = e->pos().y();
    showCoordTooltip(e->globalPosition().toPoint());
}

void RoiRectGraph::mouseRelease(QMouseEvent *e)
{
    if (!visible() || !_editing) return;
    QToolTip::hideText();
    _dragging = false;
}

void RoiRectGraph::mouseDoubleClick(QMouseEvent*)
{
    if (!visible() || !_editing) return;
    stopEdit(true);
}

void RoiRectGraph::showDragCursor(Qt::CursorShape c)
{
    if (_dragCursor == c)
        return;
    _dragCursor = c;
    QApplication::restoreOverrideCursor();
    if (c != Qt::ArrowCursor)
        QApplication::setOverrideCursor(c);
}

void RoiRectGraph::showCoordTooltip(const QPoint &p)
{
    QStringList hint;
    if (_drag0 || _dragX1) hint << QStringLiteral("X1: %1").arg(int(_x1));
    if (_drag0 || _dragX2) hint << QStringLiteral("X2: %1").arg(int(_x2));
    if (_drag0 || _dragY1) hint << QStringLiteral("Y1: %1").arg(int(_y1));
    if (_drag0 || _dragY2) hint << QStringLiteral("Y2: %1").arg(int(_y2));
    if (_dragX1 || _dragX2) hint << QStringLiteral("W: %1").arg(int(_x2) - int(_x1));
    if (_dragY1 || _dragY2) hint << QStringLiteral("H: %1").arg(int(_y2) - int(_y1));
    if (!hint.isEmpty()) QToolTip::showText(p, hint.join('\n'));
}

class RoiBoundEdit : public QSpinBox
{
public:
    RoiBoundEdit(int value, int max, int sign) : QSpinBox(), _sign(sign) {
        setMinimum(0);
        setMaximum(max);
        setValue(value);
    }
protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->modifiers().testFlag(Qt::ShiftModifier)) {
            QSpinBox::keyPressEvent(e);
            return;
        }
        switch (e->key()) {
        case Qt::Key_Up:
        case Qt::Key_Right:
            stepBy(_sign * (e->modifiers().testFlag(Qt::ControlModifier) ? 100 : 1));
            break;
        case Qt::Key_Down:
        case Qt::Key_Left:
            stepBy(_sign * (e->modifiers().testFlag(Qt::ControlModifier) ? -100 : -1));
            break;
        default:
            QSpinBox::keyPressEvent(e);
        }
    }
private:
    int _sign;
};

void RoiRectGraph::makeEditor()
{
    _seX1 = new RoiBoundEdit(_x1, _maxX, +1);
    _seY1 = new RoiBoundEdit(_y1, _maxY, -1);
    _seX2 = new RoiBoundEdit(_x2, _maxX, +1);
    _seY2 = new RoiBoundEdit(_y2, _maxY, -1);
    _seW = new RoiBoundEdit(roiW(), _maxX, 1);
    _seH = new RoiBoundEdit(roiH(), _maxY, 1);
    _seW->setReadOnly(true);
    _seH->setReadOnly(true);

    connect(_seX1, &QSpinBox::valueChanged, this, [this](int v){
        if ((int)_x1 == v) return;
        _x1 = v;
        _seW->setValue(roiW());
        parentPlot()->replot();
    });
    connect(_seX2, &QSpinBox::valueChanged, this, [this](int v){
        if ((int)_x2 == v) return;
        _x2 = v;
        _seW->setValue(roiW());
        parentPlot()->replot();
    });
    connect(_seY1, &QSpinBox::valueChanged, this, [this](int v){
        if ((int)_y1 == v) return;
        _y1 = v;
        _seH->setValue(roiH());
        parentPlot()->replot();
    });
    connect(_seY2, &QSpinBox::valueChanged, this, [this](int v){
        if ((int)_y2 == v) return;
        _y2 = v;
        _seH->setValue(roiH());
        parentPlot()->replot();
    });

    _editor = new QFrame;
    _editor->setFrameShape(QFrame::NoFrame);
    _editor->setFrameShadow(QFrame::Plain);
    auto shadow = new QGraphicsDropShadowEffect;
    if (qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        shadow->setColor(QColor(255, 255, 255, 180));
    else
        shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setBlurRadius(20);
    shadow->setOffset(0);
    _editor->setGraphicsEffect(shadow);

    auto butApply = new QToolButton;
    auto butCancel = new QToolButton;
    butApply->setIcon(QIcon(":/toolbar/check"));
    butCancel->setIcon(QIcon(":/toolbar/delete"));
    connect(butApply, &QToolButton::pressed, this, [this]{ stopEdit(true); });
    connect(butCancel, &QToolButton::pressed, this, [this]{ stopEdit(false); });
    auto buttons = new QHBoxLayout;
    buttons->addWidget(butApply);
    buttons->addWidget(butCancel);

    auto l = new QFormLayout(_editor);
    l->addRow("X1", _seX1);
    l->addRow("Y1", _seY1);
    l->addRow("X2", _seX2);
    l->addRow("Y2", _seY2);
    l->addRow("W", _seW);
    l->addRow("H", _seH);
    l->addRow("", buttons);

    _editor->setParent(parentPlot());
    _editor->setAutoFillBackground(true);
    _editor->show();
    _editor->adjustSize();
    adjustEditorPosition();
}

void RoiRectGraph::adjustEditorPosition()
{
    if (_editor)
        _editor->move(parentPlot()->width() - _editor->width() - 15, 15);
}

//------------------------------------------------------------------------------
//                               BeamInfoText
//------------------------------------------------------------------------------

BeamInfoText::BeamInfoText(QCustomPlot *parentPlot) : QCPAbstractItem(parentPlot)
{
}

void BeamInfoText::draw(QCPPainter *painter)
{
    auto r = parentPlot()->axisRect()->rect();
    painter->setPen(Qt::white);
    painter->drawText(QRect(r.left()+15, r.top()+10, 10, 10), Qt::TextDontClip, _text);
}

//------------------------------------------------------------------------------
//                             PredefinedGradient
//------------------------------------------------------------------------------

PrecalculatedGradient::PrecalculatedGradient(const QString& presetFile) : QCPColorGradient()
{
    if (!loadColors(presetFile))
        applyDefaultColors();
}

bool PrecalculatedGradient::loadColors(const QString& presetFile)
{
#define WARN qWarning() << "Loading gradient" << presetFile << ':'
    QFile file(presetFile);
    if (!file.open(QIODevice::ReadOnly)) {
        WARN << file.errorString();
        return false;
    }
    int lineNo = 0;
    QTextStream stream(&file);
    QVector<QRgb> colors;
    while (!stream.atEnd()) {
        lineNo++;
        QString line  = stream.readLine();
        if (line.isEmpty()) continue;
        QStringView view(line);
        auto parts = view.split(',');
        if (parts.size() != 3) {
            WARN << "Line" << lineNo << ": Expected three values";
            continue;
        }
        bool ok;
        int r = parts.at(0).toInt(&ok);
        if (!ok) {
            WARN << "Line" << lineNo << ": R is not int";
            continue;
        }
        int g = parts.at(1).toInt(&ok);
        if (!ok) {
            WARN << "Line" << lineNo << ": G is not int";
            continue;
        }
        int b = parts.at(2).toInt(&ok);
        if (!ok) {
            WARN << "Line" << lineNo << ": B is not int";
            continue;
        }
        colors << qRgb(r, g, b);
    }
    mColorBufferInvalidated = false;
    mLevelCount = colors.size();
    mColorBuffer = colors;
    return true;
#undef WARN
}

void PrecalculatedGradient::applyDefaultColors()
{
//    Grad0 in img/gradient.svg
//    QMap<double, QColor> colors {
//        { 0.0, QColor(0x2b053e) },
//        { 0.1, QColor(0xc2077c) },
//        { 0.15, QColor(0xbe05f3) },
//        { 0.2, QColor(0x2306fb) },
//        { 0.3, QColor(0x0675db) },
//        { 0.4, QColor(0x05f9ee) },
//        { 0.5, QColor(0x04ca04) },
//        { 0.65, QColor(0xfafd05) },
//        { 0.8, QColor(0xfc8705) },
//        { 0.9, QColor(0xfc4d06) },
//        { 1.0, QColor(0xfc5004) },
//    };

//    Grad1 in img/gradient.svg
//    QMap<double, QColor> colors {
//        { 0.00, QColor(0x2b053e) },
//        { 0.05, QColor(0xc4138a) },
//        { 0.10, QColor(0x9e0666) },
//        { 0.15, QColor(0xbe05f3) },
//        { 0.20, QColor(0x2306fb) },
//        { 0.30, QColor(0x0675db) },
//        { 0.40, QColor(0x05f9ee) },
//        { 0.50, QColor(0x04ca04) },
//        { 0.65, QColor(0xfafd05) },
//        { 0.80, QColor(0xfc8705) },
//        { 0.90, QColor(0xfc4d06) },
//        { 1.00, QColor(0xfc5004) },
//    };

    // Grad2 in img/gradient.svg
    QMap<double, QColor> colors {
        { 0.0, QColor(0x2b053e) },
        { 0.075, QColor(0xd60c8a) },
        { 0.15, QColor(0xbe05f3) },
        { 0.2, QColor(0x2306fb) },
        { 0.3, QColor(0x0675db) },
        { 0.4, QColor(0x05f9ee) },
        { 0.5, QColor(0x04ca04) },
        { 0.65, QColor(0xfafd05) },
        { 0.8, QColor(0xfc8705) },
        { 0.9, QColor(0xfc4d06) },
        { 1.0, QColor(0xfc5004) },
    };

    setColorStops(colors);
}

