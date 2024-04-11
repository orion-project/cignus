#include "BeamGraphIntf.h"

#include "plot/BeamGraph.h"

#include "qcp/src/items/item-straightline.h"
#include "qcp/src/items/item-text.h"

BeamGraphIntf::BeamGraphIntf(QCPColorMap *colorMap, QCPColorScale *colorScale, BeamEllipse *beamShape,
    QCPItemText *beamInfo, QCPItemStraightLine *lineX, QCPItemStraightLine *lineY)
    : _colorMap(colorMap), _colorScale(colorScale), _beamShape(beamShape), _beamInfo(beamInfo), _lineX(lineX), _lineY(lineY)
{
}

void BeamGraphIntf::init(int w, int h)
{
    auto d = _colorMap->data();
    if (d->keySize() != w or d->valueSize() != h) {
        _beamData = new BeamColorMapData(w, h);
        _colorMap->setData(_beamData);
    } else {
        _beamData = static_cast<BeamColorMapData*>(d);
    }
}

double* BeamGraphIntf::rawData() const
{
    return _beamData->rawData();
}

void BeamGraphIntf::invalidate() const
{
    _beamData->invalidate();
}

void BeamGraphIntf::setResult(const CgnBeamResult& r, double min, double max)
{
    _res = r;
    refreshResult();
    _colorScale->setDataRange(QCPRange(min, max));
}

void BeamGraphIntf::refreshResult()
{
    auto phi = qDegreesToRadians(_res.phi);
    auto cos_phi = cos(phi);
    auto sin_phi = sin(phi);

    _lineX->point1->setCoords(_res.xc, _res.yc);
    _lineX->point2->setCoords(_res.xc + _res.dx*cos_phi, _res.yc + _res.dx*sin_phi);

    _lineY->point1->setCoords(_res.xc, _res.yc);
    _lineY->point2->setCoords(_res.xc + _res.dy*sin_phi, _res.yc - _res.dy*cos_phi);

    _beamShape->xc = _res.xc;
    _beamShape->yc = _res.yc;
    _beamShape->dx = _res.dx;
    _beamShape->dy = _res.dy;
    _beamShape->phi = _res.phi;

    if (_beamInfo->visible()) {
        double eps = qMin(_res.dx, _res.dy) / qMax(_res.dx, _res.dy);
        _beamInfo->setText(QStringLiteral("Xc=%1\nYc=%2\nDx=%3\nDy=%4\nφ=%5°\nε=%6")
            .arg(int(_res.xc)).arg(int(_res.yc)).arg(int(_res.dx)).arg(int(_res.dy))
            .arg(_res.phi, 0, 'f', 1).arg(eps, 0, 'f', 3));
    }
}
