#ifndef CAMERA_BASE_H
#define CAMERA_BASE_H

#include <QPointer>
#include <QString>

#include "cameras/CameraTypes.h"

class MeasureSaver;
class PlotIntf;
class TableIntf;

class QWidget;

namespace Ori::Dlg {
struct ConfigDlgOpts;
}

class Camera
{
public:
    virtual ~Camera() {}

    virtual QString name() const = 0;
    virtual QString descr() const { return {}; }
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int bpp() const = 0;
    virtual PixelScale sensorScale() const { return {}; }

    virtual bool isCapturing() const { return false; }
    virtual void startCapture() = 0;
    virtual void stopCapture() {}

    virtual bool canMeasure() const { return false; }
    virtual void startMeasure(MeasureSaver*) {}
    virtual void stopMeasure() {}

    virtual bool canHardConfig() const { return false; }
    virtual void saveHardConfig(QSettings*) {}
    virtual QPointer<QWidget> showHardConfgWindow() { return {}; }

    virtual void requestRawImg(QObject *sender) {}

    const CameraConfig& config() const { return _config; }
    enum ConfigPages { cfgPlot, cfgBgnd, cfgRoi, cfgMax };
    bool editConfig(int page = -1);
    
    void setAperture(const RoiRect&);
    void toggleAperture(bool on);
    bool isRoiValid() const;

    PixelScale pixelScale() const;
    QString resolutionStr() const;

protected:
    PlotIntf *_plot;
    TableIntf *_table;
    QString _configGroup;
    CameraConfig _config;

    Camera(PlotIntf *plot, TableIntf *table, const char* configGroup);

    virtual void initConfigMore(Ori::Dlg::ConfigDlgOpts &opts) {}
    virtual void saveConfigMore() {}

    void loadConfig();
    void saveConfig();
};

#endif // CAMERA_BASE_H
