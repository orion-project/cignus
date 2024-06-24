#ifndef CAMERA_TYPES_H
#define CAMERA_TYPES_H

#include <QEvent>
#include <QVariant>

class QSettings;

QString formatSecs(int secs);

struct CameraItem
{
    QVariant id;
    QString name;
};

struct RoiRect
{
    bool on = false;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;

    int width() const { return x2 - x1; }
    int height() const { return y2 - y1; }
    bool isValid(int w, int h) const;
    bool isZero() const;
    void fix(int w, int h);
    QString sizeStr() const;
};

struct PixelScale
{
    bool on = false;
    double factor = 1;
    QString unit = "um";

    inline double pixelToUnit(const double& v) const { return on ? v*factor : v; }
    inline double unitToPixel(const double& v) const { return on ? v/factor : v; }

    bool operator ==(const PixelScale& s) const;
    bool operator !=(const PixelScale& s) const { return !(*this == s); }

    QString format(const double &v) const;
    QString formatWithMargins(const double &v) const;
};

struct Background
{
    bool on = true;
    int iters = 0;
    double precision = 0.05;
    double corner = 0.035;
    double noise = 3;
    double mask = 3;
};

struct PlotOptions
{
    bool normalize = true;
    bool rescale = false;
    bool fullRange = true;
    PixelScale customScale;
};

struct CameraConfig
{
    PlotOptions plot;
    Background bgnd;
    RoiRect roi;

    void load(QSettings *s);
    void save(QSettings *s, bool min=false) const;
};

struct CameraStats
{
    double fps;
    double hardFps = 0;
    qint64 measureTime;
    QString errorFrames;
};

class BrightEvent : public QEvent
{
public:
    BrightEvent() : QEvent(QEvent::User) {}

    double level;
};

#endif // CAMERA_TYPES_H
