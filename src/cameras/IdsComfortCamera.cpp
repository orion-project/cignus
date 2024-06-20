#include "IdsComfortCamera.h"

#ifdef WITH_IDS

#define LOG_ID "IdsComfortCamera:"

#include "app/AppSettings.h"
#include "cameras/CameraWorker.h"
#include "cameras/HardConfigPanel.h"
#include "cameras/IdsLib.h"

#include "dialogs/OriConfigDlg.h"
#include "helpers/OriDialogs.h"
#include "tools/OriSettings.h"


#define FRAME_TIMEOUT 5000
//#define LOG_FRAME_TIME

//------------------------------------------------------------------------------
//                              IdsComfort
//------------------------------------------------------------------------------

#define IDS IdsLib::instance()

IdsComfort* IdsComfort::init()
{
    return IDS.loaded() ? new IdsComfort() : nullptr;
}

IdsComfort::~IdsComfort()
{
    if (IDS.loaded())
        IDS.unload();
}

static QString makeCameraName(const peak_camera_descriptor &cam)
{
    return QString("%1 (*%2)").arg(
        QString::fromLatin1(cam.modelName),
        QString::fromLatin1(cam.serialNumber).right(4));
}

QVector<CameraItem> IdsComfort::getCameras()
{
    size_t camCount;
    auto res = IDS.peak_CameraList_Update(&camCount);
    if (PEAK_ERROR(res)) {
        qWarning() << LOG_ID << "Unable to update camera list" << IDS.getPeakError(res);
        return {};
    }
    else qDebug() << LOG_ID << "Camera list updated. Cameras found:" << camCount;

    QVector<peak_camera_descriptor> cams(camCount);
    res = IDS.peak_CameraList_Get(cams.data(), &camCount);
    if (PEAK_ERROR(res)) {
        qWarning() << LOG_ID << "Unable to get camera list" << IDS.getPeakError(res);
        return {};
    }

    QVector<CameraItem> result;
    for (const auto &cam : cams) {
        qDebug() << LOG_ID << cam.cameraID << cam.cameraType << cam.modelName << cam.serialNumber;
        result << CameraItem {
            .id = cam.cameraID,
            .name = makeCameraName(cam)
        };
    }
    return result;
}

//------------------------------------------------------------------------------
//                              PeakIntf
//------------------------------------------------------------------------------

#define CHECK_ERR(msg) \
    if (PEAK_ERROR(res)) { \
        auto err = IDS.getPeakError(res); \
        qCritical() << LOG_ID << msg << err; \
        return QString(msg ": ") + err; \
    }

#define GFA_GET_FLOAT(prop, value) \
    if (PEAK_IS_READABLE(IDS.peak_GFA_Feature_GetAccessStatus(hCam, PEAK_GFA_MODULE_REMOTE_DEVICE, prop))) { \
        res = IDS.peak_GFA_Float_Get(hCam, PEAK_GFA_MODULE_REMOTE_DEVICE, prop, &value); \
        if (PEAK_ERROR(res)) { \
            qWarning() << LOG_ID << "Failed to read" << prop << IDS.getPeakError(res); \
            break; \
        } \
        qDebug() << LOG_ID << prop << value; \
    } else { \
        qDebug() << LOG_ID << prop << "is not readable"; \
        break; \
    }

#define SHOW_CAM_PROP(prop, func, typ) {\
    typ val; \
    auto res = func(hCam, &val); \
    if (PEAK_ERROR(res)) \
        qDebug() << LOG_ID << "Unable to get" << prop << IDS.getPeakError(res); \
    else qDebug() << LOG_ID << prop << val; \
}

class PeakIntf : public CameraWorker
{
public:
    peak_camera_id id;
    IdsComfortCamera *cam;
    peak_camera_handle hCam = PEAK_INVALID_HANDLE;
    peak_status res;
    peak_buffer buf;
    peak_frame_handle frame;
    int errCount = 0;
    QSet<int> supportedBpp;

    QByteArray hdrBuf;

    PeakIntf(peak_camera_id id, PlotIntf *plot, TableIntf *table, IdsComfortCamera *cam)
        : CameraWorker(plot, table, cam, cam), id(id), cam(cam)
    {}

    QString init()
    {
        peak_camera_descriptor descr;
        res = IDS.peak_Camera_GetDescriptor(id, &descr);
        CHECK_ERR("Unable to get camera descriptor");
        cam->_name = makeCameraName(descr);
        cam->_descr = cam->_name + ' ' + QString::fromLatin1(descr.serialNumber);
        cam->_configGroup = cam->_name + '-' + QString::fromLatin1(descr.serialNumber);
        cam->loadConfig();
        cam->loadConfigMore();

        res = IDS.peak_Camera_Open(id, &hCam);
        CHECK_ERR("Unable to open camera");
        qDebug() << LOG_ID << "Camera opened" << id;

        //----------------- Init resolution reduction

        cam->_cfg.binningX = 1;
        cam->_cfg.binningY = 1;
        cam->_cfg.binningsX = {1, 2};
        cam->_cfg.binningsY = {1, 2};
        cam->_cfg.decimX = 1;
        cam->_cfg.decimY = 1;
        cam->_cfg.decimsX = {1, 2};
        cam->_cfg.decimsY = {1, 2};

        //----------------- Get image size

        peak_size roiMin, roiMax, roiInc;
        res = IDS.peak_ROI_Size_GetRange(hCam, &roiMin, &roiMax, &roiInc);
        CHECK_ERR("Unable to get ROI range");
        qDebug() << LOG_ID << "ROI"
            << QString("min=%1x%24").arg(roiMin.width).arg(roiMin.height)
            << QString("max=%1x%24").arg(roiMax.width).arg(roiMax.height)
            << QString("inc=%1x%24").arg(roiInc.width).arg(roiInc.height);

        peak_roi roi = {0, 0, 0, 0};
        res = IDS.peak_ROI_Get(hCam, &roi);
        CHECK_ERR("Unable to get ROI");
        qDebug() << LOG_ID << "ROI"
            << QString("size=%1x%2").arg(roi.size.width).arg(roi.size.height)
            << QString("offset=%1x%2").arg(roi.offset.x).arg(roi.offset.y);
        if (roi.size.width != roiMax.width || roi.size.height != roiMax.height) {
            roi.offset.x = 0;
            roi.offset.y = 0;
            roi.size.width = roiMax.width;
            roi.size.height = roiMax.height;
            qDebug() << LOG_ID << "Set ROI"
                << QString("size=%1x%2").arg(roiMax.width).arg(roiMax.height);
            res = IDS.peak_ROI_Set(hCam, roi);
            CHECK_ERR("Unable to set ROI");
        }
        c.w = roi.size.width;
        c.h = roi.size.height;
        cam->_width = c.w;
        cam->_height = c.h;

        while (true) {
            double pixelW, pixelH;
            GFA_GET_FLOAT("SensorPixelWidth", pixelW);
            GFA_GET_FLOAT("SensorPixelHeight", pixelH);
            if (pixelW != pixelH) {
                qWarning() << LOG_ID << "Non-square pixels are not supported";
                break;
            }
            cam->_pixelScale.on = true;
            cam->_pixelScale.factor = pixelW;
            break;
        }

        //----------------- Init pixel format

        size_t formatCount = 0;
        res = IDS.peak_PixelFormat_GetList(hCam, nullptr, &formatCount);
        CHECK_ERR("Unable to get pixel format count");
        QVector<peak_pixel_format> pixelFormats(formatCount);
        res = IDS.peak_PixelFormat_GetList(hCam, pixelFormats.data(), &formatCount);
        CHECK_ERR("Unable to get pixel formats");
        QMap<int, peak_pixel_format> supportedFormats;
        for (int i = 0; i < formatCount; i++) {
            auto pf = pixelFormats.at(i);
            qDebug() << LOG_ID << "Supported pixel format" << QString::number(pf, 16);
            if (pf == PEAK_PIXEL_FORMAT_MONO8) {
                supportedBpp << 8;
                supportedFormats.insert(8, pf);
            } else if (pf == PEAK_PIXEL_FORMAT_MONO10G40_IDS) {
                supportedBpp << 10;
                supportedFormats.insert(10, pf);
            } else if (pf == PEAK_PIXEL_FORMAT_MONO12G24_IDS) {
                supportedBpp << 12;
                supportedFormats.insert(12, pf);
            }
        }
        if (supportedFormats.empty()) {
            return "Camera doesn't support any of known gray scale formats (Mono8, Mono10g40, Mono12g24)";
        }
        c.bpp = cam->_bpp;
        peak_pixel_format targetFormat;
        if (!supportedFormats.contains(c.bpp)) {
            c.bpp = supportedFormats.firstKey();
            targetFormat = supportedFormats.first();
            qWarning() << LOG_ID << "Camera does not support " << cam->_bpp << "bpp, use " << c.bpp << "bpp";
        } else {
            targetFormat = supportedFormats[c.bpp];
        }
        qDebug() << LOG_ID << "Set pixel format" << targetFormat << c.bpp << "bpp";
        res = IDS.peak_PixelFormat_Set(hCam, targetFormat);
        CHECK_ERR("Unable to set pixel format");
        cam->_bpp = c.bpp;
        if (c.bpp > 8) {
            hdrBuf = QByteArray(c.w*c.h*2, 0);
            c.buf = (uint8_t*)hdrBuf.data();
        }

        //----------------- Show some current props

        size_t chanCount;
        res = IDS.peak_Gain_GetChannelList(hCam, PEAK_GAIN_TYPE_ANALOG, nullptr, &chanCount);
        CHECK_ERR("Unable to get gain channel list count");
        QList<peak_gain_channel> chans(chanCount);
        res = IDS.peak_Gain_GetChannelList(hCam, PEAK_GAIN_TYPE_ANALOG, chans.data(), &chanCount);
        CHECK_ERR("Unable to get gain channel list");
        for (auto chan : chans)
            qDebug() << LOG_ID << "Gain channel" << chan;

        SHOW_CAM_PROP("FPS", IDS.peak_FrameRate_Get, double);
        SHOW_CAM_PROP("Exposure", IDS.peak_ExposureTime_Get, double);

        //----------------- Init graph and calcs

        plot->initGraph(c.w, c.h);
        graph = plot->rawGraph();

        configure();

        //----------------- Start

        res = IDS.peak_Acquisition_Start(hCam, PEAK_INFINITE);
        CHECK_ERR("Unable to start acquisition");
        qDebug() << LOG_ID << "Acquisition started";

        return {};
    }

    ~PeakIntf()
    {
        if (hCam == PEAK_INVALID_HANDLE)
            return;

        if (IDS.peak_Acquisition_IsStarted(hCam))
        {
            auto res = IDS.peak_Acquisition_Stop(hCam);
            if (PEAK_ERROR(res))
                qWarning() << LOG_ID << "Unable to stop acquisition";
            else qDebug() << LOG_ID << "Acquisition stopped";
        }

        auto res = IDS.peak_Camera_Close(hCam);
        if (PEAK_ERROR(res))
            qWarning() << LOG_ID << "Unable to close camera" << IDS.getPeakError(res);
        else qDebug() << LOG_ID << "Camera closed" << id;
        hCam = PEAK_INVALID_HANDLE;
    }

    void run()
    {
        qDebug() << LOG_ID << "Started" << QThread::currentThreadId();
        start = QDateTime::currentDateTime();
        timer.start();
        while (true) {
            if (waitFrame()) continue;

            tm = timer.elapsed();
            res = IDS.peak_Acquisition_WaitForFrame(hCam, FRAME_TIMEOUT, &frame);
            if (PEAK_SUCCESS(res))
                res = IDS.peak_Frame_Buffer_Get(frame, &buf);
            if (res == PEAK_STATUS_ABORTED) {
                auto err = IDS.getPeakError(res);
                qCritical() << LOG_ID << "Interrupted" << err;
                emit cam->error("Interrupted: " + err);
                return;
            }
            markRenderTime();

            if (res == PEAK_STATUS_SUCCESS) {
                tm = timer.elapsed();
                if (c.bpp == 12)
                    cgn_convert_12g24_to_u16(c.buf, buf.memoryAddress, buf.memorySize);
                else if (c.bpp == 10)
                    cgn_convert_10g40_to_u16(c.buf, buf.memoryAddress, buf.memorySize);
                else
                    c.buf = buf.memoryAddress;
                calcResult();
                markCalcTime();

                if (showResults())
                    emit cam->ready();

                res = IDS.peak_Frame_Release(hCam, frame);
                if (PEAK_ERROR(res)) {
                    auto err = IDS.getPeakError(res);
                    qCritical() << LOG_ID << "Unable to release frame" << err;
                    emit cam->error("Unable to release frame: " + err);
                    return;
                }
            } else {
                errCount++;
                stats[QStringLiteral("errorFrame")] = errCount;
                QString errKey = QStringLiteral("frameError_") + QString::number(res, 16);
                stats[errKey] = stats[errKey].toInt() + 1;
            }

            if (tm - prevStat >= STAT_DELAY_MS) {
                prevStat = tm;

                QStringList errors;
                errors << QString::number(errCount);

                // TODO: suggest a way for sending arbitrary stats and showing it in the table
                peak_acquisition_info info;
                memset(&info, 0, sizeof(info));
                res = IDS.peak_Acquisition_GetInfo(hCam, &info);
                if (PEAK_SUCCESS(res)) {
                    errors << QString::number(info.numUnderrun)
                            << QString::number(info.numDropped)
                            << QString::number(info.numIncomplete);
                    stats[QStringLiteral("underrunFrames")] = info.numUnderrun;
                    stats[QStringLiteral("droppedFrames")] = info.numDropped;
                    stats[QStringLiteral("incompleteFrames")] = info.numIncomplete;
                }

                double ft = avgFrameTime / avgFrameCount;
                avgFrameTime = 0;
                avgFrameCount = 0;
                CameraStats st {
                    .fps = qRound(1000.0/ft),
                    .measureTime = measureStart > 0 ? timer.elapsed() - measureStart : -1,
                    .errorFrames = errors.join(','),
                };
                emit cam->stats(st);

#ifdef LOG_FRAME_TIME
                qDebug()
                    << "FPS:" << st.fps
                    << "avgFrameTime:" << qRound(ft)
                    << "avgRenderTime:" << qRound(avgRenderTime)
                    << "avgCalcTime:" << qRound(avgCalcTime)
                    << "errCount: " << errCount
                    << IDS.getPeakError(res);
#endif
                if (cam->isInterruptionRequested()) {
                    qDebug() << LOG_ID << "Interrupted by user";
                    return;
                }
                checkReconfig();
            }
        }
    }

    QString gfaGetStr(const char* prop)
    {
        auto mod = PEAK_GFA_MODULE_REMOTE_DEVICE;
        if (!PEAK_IS_READABLE(IDS.peak_GFA_Feature_GetAccessStatus(hCam, mod, prop)))
            return "Is not readable";
        size_t size;
        auto res = IDS.peak_GFA_String_Get(hCam, mod, prop, nullptr, &size);
        if (PEAK_ERROR(res))
            return IDS.getPeakError(res);
        QByteArray buf(size, 0);
        res = IDS.peak_GFA_String_Get(hCam, mod, prop, buf.data(), &size);
        if (PEAK_ERROR(res))
            return IDS.getPeakError(res);
        qDebug() << "Reading" << prop << 3;
        return QString::fromLatin1(buf);
    }
};

//------------------------------------------------------------------------------
//                              IdsComfortCamera
//------------------------------------------------------------------------------

IdsComfortCamera::IdsComfortCamera(QVariant id, PlotIntf *plot, TableIntf *table, QObject *parent) :
    Camera(plot, table, "IdsComfortCamera"), QThread(parent), _id(id)
{
    auto peak = new PeakIntf(id.value<peak_camera_id>(), plot, table, this);
    auto res = peak->init();
    if (!res.isEmpty())
    {
        Ori::Dlg::error(res);
        delete peak;
        return;
    }
    _peak.reset(peak);

    connect(parent, SIGNAL(camConfigChanged()), this, SLOT(camConfigChanged()));
}

IdsComfortCamera::~IdsComfortCamera()
{
    qDebug() << LOG_ID << "Deleted";
}

PixelScale IdsComfortCamera::sensorScale() const
{
    return _pixelScale;
}

void IdsComfortCamera::startCapture()
{
    start();
}

void IdsComfortCamera::stopCapture()
{
    if (_peak)
        _peak.reset(nullptr);
}

void IdsComfortCamera::startMeasure(MeasureSaver *saver)
{
    if (_peak)
        _peak->startMeasure(saver);
}

void IdsComfortCamera::stopMeasure()
{
    if (_peak)
        _peak->stopMeasure();
}

void IdsComfortCamera::run()
{
    if (_peak)
        _peak->run();
}

void IdsComfortCamera::camConfigChanged()
{
    if (_peak)
        _peak->reconfigure();
}

void IdsComfortCamera::saveHardConfig(QSettings *s)
{
    if (!_peak)
        return;
    double v;
    auto res = IDS.peak_ExposureTime_Get(_peak->hCam, &v);
    if (PEAK_ERROR(res))
        s->setValue("exposure", IDS.getPeakError(res));
    else s->setValue("exposure", v);
    res = IDS.peak_FrameRate_Get(_peak->hCam, &v);
    if (PEAK_ERROR(res))
        s->setValue("frameRate", IDS.getPeakError(res));
    else s->setValue("frameRate", v);
}

void IdsComfortCamera::requestRawImg(QObject *sender)
{
    if (_peak)
        _peak->requestRawImg(sender);
}

void IdsComfortCamera::setRawView(bool on, bool reconfig)
{
    if (_peak)
        _peak->setRawView(on, reconfig);
}

#include <QComboBox>

#include "helpers/OriLayouts.h"

using namespace Ori::Dlg;

class ConfigEditorXY : public ConfigItemEditor
{
public:
    ConfigEditorXY(const QList<quint32> &xs, const QList<quint32> &ys, quint32 *x, quint32 *y) : ConfigItemEditor(), x(x), y(y) {
        comboX = new QComboBox;
        comboY = new QComboBox;
        fillCombo(comboX, xs, *x);
        fillCombo(comboY, ys, *y);
        Ori::Layouts::LayoutH({
            QString("X:"), comboX,
            Ori::Layouts::SpaceH(4),
            QString("Y:"), comboY, Ori::Layouts::Stretch(),
        }).useFor(this);
    }

    void collect() override {
        *x = qMax(1u, comboX->currentData().toUInt());
        *y = qMax(1u, comboY->currentData().toUInt());
    }

    void fillCombo(QComboBox *combo, const QList<quint32> &items, quint32 cur) {
        int idx = -1;
        for (int i = 0; i < items.size(); i++) {
            quint32 v = items[i];
            combo->addItem(QString::number(v), v);
            if (v == cur)
                idx = i;
        }
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }

    quint32 *x, *y;
    QComboBox *comboX, *comboY;
};

void IdsComfortCamera::initConfigMore(Ori::Dlg::ConfigDlgOpts &opts)
{
    int pageHard = cfgMax+1;
    int pageInfo = cfgMax+2;
    _cfg.bpp8 = _bpp == 8;
    _cfg.bpp10 = _bpp == 10;
    _cfg.bpp12 = _bpp == 12;
    opts.pages << ConfigPage(pageHard, tr("Hardware"), ":/toolbar/hardware");
    opts.items
        << (new ConfigItemSection(pageHard, tr("Pixel format")))
            ->withHint(tr("Reselect camera to apply"))
        << (new ConfigItemBool(pageHard, tr("8 bit"), &_cfg.bpp8))
            ->setDisabled(!_peak->supportedBpp.contains(8))->withRadioGroup("pixel_format")
        << (new ConfigItemBool(pageHard, tr("10 bit"), &_cfg.bpp10))
            ->setDisabled(!_peak->supportedBpp.contains(10))->withRadioGroup("pixel_format")
        << (new ConfigItemBool(pageHard, tr("12 bit"), &_cfg.bpp12))
            ->setDisabled(!_peak->supportedBpp.contains(12))->withRadioGroup("pixel_format")
    ;

    opts.items
        << new ConfigItemSpace(pageHard, 12)
        << (new ConfigItemSection(pageHard, tr("Resolution reduction")))
            ->withHint(tr("Reselect camera to apply"));
    if (_cfg.binningX > 0)
        opts.items << new ConfigItemCustom(pageHard, tr("Binning"), new ConfigEditorXY(
            _cfg.binningsX, _cfg.binningsY, &_cfg.binningX, &_cfg.binningY));
    else
        opts.items << (new ConfigItemEmpty(pageHard, tr("Binning")))
            ->withHint(tr("Is not configurable"));
    if (_cfg.decimX > 0)
        opts.items << new ConfigItemCustom(pageHard, tr("Decimation"), new ConfigEditorXY(
            _cfg.decimsX, _cfg.decimsY, &_cfg.decimX, &_cfg.decimY));
    else
        opts.items << (new ConfigItemEmpty(pageHard, tr("Decimation")))
            ->withHint(tr("Is not configurable"));

    if (_peak) {

        if (!_cfg.intoRequested) {
            _cfg.intoRequested = true;
            _cfg.infoModelName = _peak->gfaGetStr("DeviceModelName");
            _cfg.infoFamilyName = _peak->gfaGetStr("DeviceFamilyName");
            _cfg.infoSerialNum = _peak->gfaGetStr("DeviceSerialNumber");
            _cfg.infoVendorName = _peak->gfaGetStr("DeviceVendorName");
            _cfg.infoManufacturer = _peak->gfaGetStr("DeviceManufacturerInfo");
            _cfg.infoDeviceVer = _peak->gfaGetStr("DeviceVersion");
            _cfg.infoFirmwareVer = _peak->gfaGetStr("DeviceFirmwareVersion");
        }
        opts.pages << ConfigPage(pageInfo, tr("Info"), ":/toolbar/info");
        opts.items
            << (new ConfigItemStr(pageInfo, tr("Model name"), &_cfg.infoModelName))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Family name"), &_cfg.infoFamilyName))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Serial number"), &_cfg.infoSerialNum))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Vendor name"), &_cfg.infoVendorName))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Manufacturer info"), &_cfg.infoManufacturer))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Device version"), &_cfg.infoDeviceVer))->withReadOnly()
            << (new ConfigItemStr(pageInfo, tr("Firmware version"), &_cfg.infoFirmwareVer))->withReadOnly()
        ;
    }
}

void IdsComfortCamera::saveConfigMore()
{
    Ori::Settings s;
    s.beginGroup(_configGroup);
    s.setValue("hard.bpp", _cfg.bpp12 ? 12 : _cfg.bpp10 ? 10 : 8);
    s.setValue("hard.binning.x", _cfg.binningX);
    s.setValue("hard.binning.y", _cfg.binningY);
    s.setValue("hard.decim.x", _cfg.decimX);
    s.setValue("hard.decim.y", _cfg.decimY);
}

void IdsComfortCamera::loadConfigMore()
{
    Ori::Settings s;
    s.beginGroup(_configGroup);
    _bpp = s.value("hard.bpp", 8).toInt();
    _cfg.binningX = s.value("hard.binning.x").toUInt();
    _cfg.binningY = s.value("hard.binning.y").toUInt();
    _cfg.decimX = s.value("hard.decim.x").toUInt();
    _cfg.decimY = s.value("hard.decim.y").toUInt();
}

//------------------------------------------------------------------------------
//                             IdsHardConfigWindow
//------------------------------------------------------------------------------

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleHints>
#include <QWheelEvent>

#include "helpers/OriLayouts.h"
#include "tools/OriSettings.h"
#include "widgets/OriValueEdit.h"

using namespace Ori::Layouts;
using namespace Ori::Widgets;

#define PROP_CONTROL(Prop, title) { \
    auto label = new QLabel; \
    label->setWordWrap(true); \
    label->setForegroundRole(qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QPalette::Light : QPalette::Mid); \
    auto edit = new CamPropEdit; \
    edit->scrolled = [this](bool wheel, bool inc, bool big){ set##Prop##Fast(wheel, inc, big); }; \
    edit->connect(edit, &ValueEdit::keyPressed, edit, [this](int key){ \
        if (key == Qt::Key_Return || key == Qt::Key_Enter) set##Prop(); }); \
    auto btn = new QPushButton(tr("Set")); \
    btn->setFixedWidth(50); \
    btn->connect(btn, &QPushButton::pressed, btn, [this]{ set##Prop(); }); \
    auto group = LayoutV({label, LayoutH({edit, btn})}).makeGroupBox(title); \
    groups << group; \
    layout->addWidget(group); \
    lab##Prop = label; \
    ed##Prop = edit; \
    group##Prop = group; \
}

#define CHECK_PROP_STATUS(Prop, getStatus) \
    if (getStatus(hCam) != PEAK_ACCESS_READWRITE) { \
        lab##Prop->setText(tr("Not configurable")); \
        ed##Prop->setDisabled(true); \
    } else show##Prop();

#define PROP(Prop, setProp, getProp, getRange) \
    QLabel *lab##Prop; \
    CamPropEdit *ed##Prop; \
    QGroupBox *group##Prop; \
    \
    void show##Prop() { \
        auto edit = ed##Prop; \
        auto label = lab##Prop; \
        double value, min, max, step; \
        auto res = getProp(hCam, &value); \
        if (PEAK_ERROR(res)) { \
            label->setText(IDS.getPeakError(res)); \
            edit->setValue(0); \
            edit->setDisabled(true); \
            props[#Prop] = 0; \
            return; \
        } \
        edit->setValue(value); \
        edit->setDisabled(false); \
        props[#Prop] = value; \
        res = getRange(hCam, &min, &max, &step); \
        if (PEAK_ERROR(res)) \
            label->setText(IDS.getPeakError(res)); \
        else { \
            label->setText(QString("<b>Min = %1, Max = %2</b>").arg(min, 0, 'f', 2).arg(max, 0, 'f', 2)); \
            props[#Prop "Min"] = min; \
            props[#Prop "Max"] = max; \
            props[#Prop "Step"] = step; \
        }\
        if (ed##Prop == edExp) \
            showExpFreq(value); \
    } \
    void set##Prop() { \
        set##Prop##Raw(ed##Prop->value()); \
    } \
    bool set##Prop##Raw(double v) { \
        auto res = setProp(hCam, v); \
        if (PEAK_ERROR(res)) { \
            Ori::Dlg::error(IDS.getPeakError(res)); \
            return false; \
        } \
        if (ed##Prop == edFps || ed##Prop == edExp) { \
            showExp(); \
            showFps(); \
        } else show##Prop(); \
        return true; \
    } \
    void set##Prop##Fast(bool wheel, bool inc, bool big) { \
        double change = wheel \
            ? (big ? propChangeWheelBig : propChangeWheelSm) \
            : (big ? propChangeArrowBig : propChangeArrowSm); \
        double step = props[#Prop "Step"]; \
        double val = props[#Prop]; \
        double newVal; \
        if (inc) { \
            double max = props[#Prop "Max"]; \
            if (val >= max) return; \
            newVal = val * change; \
            if (newVal - val < step) newVal = val + step; \
            newVal = qMin(max, newVal); \
        } else { \
            double min = props[#Prop "Min"]; \
            if (val <= min) return; \
            newVal = val / change; \
            qDebug() << #Prop << "min=" << min << "step=" << step << "val=" << val; \
            if (val - newVal < step) newVal = val - step; \
            newVal = qMax(min, newVal); \
            qDebug() << #Prop << "set=" << newVal; \
        } \
        auto res = setProp(hCam, newVal); \
        if (PEAK_ERROR(res)) \
            Ori::Dlg::error(IDS.getPeakError(res)); \
        if (ed##Prop == edFps || ed##Prop == edExp) { \
            showExp(); \
            showFps(); \
        } else show##Prop(); \
    }

class CamPropEdit : public ValueEdit
{
public:
    std::function<void(bool, bool, bool)> scrolled;
protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Up) {
            scrolled(false, true, e->modifiers().testFlag(Qt::ControlModifier));
            e->accept();
        } else if (e->key() == Qt::Key_Down) {
            scrolled(false, false, e->modifiers().testFlag(Qt::ControlModifier));
            e->accept();
        } else
            ValueEdit::keyPressEvent(e);
    }
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus()) {
            scrolled(true, e->angleDelta().y() > 0, e->modifiers().testFlag(Qt::ControlModifier));
            e->accept();
        } else
            ValueEdit::wheelEvent(e);
    }
};

namespace {

peak_status setAnalogGain(peak_camera_handle hCam, double v) {
    return IDS.peak_Gain_Set(hCam, PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER, v);
}
peak_status setDigitalGain(peak_camera_handle hCam, double v) {
    return IDS.peak_Gain_Set(hCam, PEAK_GAIN_TYPE_DIGITAL, PEAK_GAIN_CHANNEL_MASTER, v);
}
peak_status getAnalogGain(peak_camera_handle hCam, double *v) {
    return IDS.peak_Gain_Get(hCam, PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER, v);
}
peak_status getDigitalGain(peak_camera_handle hCam, double *v) {
    return IDS.peak_Gain_Get(hCam, PEAK_GAIN_TYPE_DIGITAL, PEAK_GAIN_CHANNEL_MASTER, v);
}
peak_status getAnalogGainRange(peak_camera_handle hCam, double *min, double *max, double *inc) {
    return IDS.peak_Gain_GetRange(hCam, PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER, min, max, inc);
}
peak_status getDigitalGainRange(peak_camera_handle hCam, double *min, double *max, double *inc) {
    return IDS.peak_Gain_GetRange(hCam, PEAK_GAIN_TYPE_DIGITAL, PEAK_GAIN_CHANNEL_MASTER, min, max, inc);
}
peak_access_status getAnalogGainAccessStatus(peak_camera_handle hCam) {
    return IDS.peak_Gain_GetAccessStatus(hCam, PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER);
}
peak_access_status getDigitlGainAccessStatus(peak_camera_handle hCam) {
    return IDS.peak_Gain_GetAccessStatus(hCam, PEAK_GAIN_TYPE_DIGITAL, PEAK_GAIN_CHANNEL_MASTER);
}

} // namespace

class IdsHardConfigPanel: public HardConfigPanel, public IAppSettingsListener
{
    Q_DECLARE_TR_FUNCTIONS(IdsHardConfigWindow)

public:
    IdsHardConfigPanel(PeakIntf *peak, QWidget *parent) : HardConfigPanel(parent), peak(peak)
    {
        hCam = peak->hCam;

        auto widget = new QWidget;
        auto layout = new QVBoxLayout(widget);

        PROP_CONTROL(Exp, tr("Exposure (us)"))

        labExpFreq = new QLabel;
        groupExp->layout()->addWidget(labExpFreq);

        {
            auto label = new QLabel(tr("Percent of dynamic range:"));
            edAutoExp = new CamPropEdit;
            edAutoExp->connect(edAutoExp, &ValueEdit::keyPressed, edAutoExp, [this](int key){
                if (key == Qt::Key_Return || key == Qt::Key_Enter) autoExposure(); });
            butAutoExp = new QPushButton(tr("Find"));
            butAutoExp->setFixedWidth(50);
            butAutoExp->connect(butAutoExp, &QPushButton::pressed, butAutoExp, [this]{ autoExposure(); });
            auto group = LayoutV({label, LayoutH({edAutoExp, butAutoExp})}).makeGroupBox(tr("Autoexposure"));
            groups << group;
            layout->addWidget(group);
        }

        PROP_CONTROL(Fps, tr("Frame rate"));
        PROP_CONTROL(AnalogGain, tr("Analog gain"))
        PROP_CONTROL(DigitalGain, tr("Digital gain"))

        CHECK_PROP_STATUS(Exp, IDS.peak_ExposureTime_GetAccessStatus)
        CHECK_PROP_STATUS(Fps, IDS.peak_FrameRate_GetAccessStatus)
        CHECK_PROP_STATUS(AnalogGain, getAnalogGainAccessStatus)
        CHECK_PROP_STATUS(DigitalGain, getDigitlGainAccessStatus)

        vertMirror = new QCheckBox(tr("Mirror vertically"));
        if (IDS.peak_Mirror_UpDown_GetAccessStatus(hCam) == PEAK_ACCESS_READWRITE) {
            vertMirror->setChecked(IDS.peak_Mirror_UpDown_IsEnabled(hCam));
        } else {
            vertMirror->setChecked(IDS.peak_IPL_Mirror_UpDown_IsEnabled(hCam));
            vertMirrorIPL = true;
        }
        connect(vertMirror, &QCheckBox::toggled, this, &IdsHardConfigPanel::toggleVertMirror);
        layout->addWidget(vertMirror);

        horzMirror = new QCheckBox(tr("Mirror horizontally"));
        if (IDS.peak_Mirror_LeftRight_GetAccessStatus(hCam) == PEAK_ACCESS_READWRITE) {
            horzMirror->setChecked(IDS.peak_Mirror_LeftRight_IsEnabled(hCam));
        } else {
            horzMirror->setChecked(IDS.peak_IPL_Mirror_LeftRight_IsEnabled(hCam));
            horzMirrorIPL = true;
        }
        connect(horzMirror, &QCheckBox::toggled, this, &IdsHardConfigPanel::toggleHorzMirror);
        layout->addWidget(horzMirror);

        // TODO: mirroring doesn't work, while works in IDS Cockpit for the same camera
        vertMirror->setVisible(false);
        horzMirror->setVisible(false);

        layout->addStretch();

        auto scroll = new QScrollArea;
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(widget);

        auto mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(scroll);

        applySettings();
    }

    PROP(Exp, IDS.peak_ExposureTime_Set, IDS.peak_ExposureTime_Get, IDS.peak_ExposureTime_GetRange)
    PROP(Fps, IDS.peak_FrameRate_Set, IDS.peak_FrameRate_Get, IDS.peak_FrameRate_GetRange)
    PROP(AnalogGain, ::setAnalogGain, getAnalogGain, getAnalogGainRange)
    PROP(DigitalGain, ::setDigitalGain, getDigitalGain, getDigitalGainRange)

    void showExpFreq(double exp)
    {
        double freq = 1e6 / exp;
        QString s;
        if (freq < 1000)
            s = QStringLiteral("Corresponds to <b>%1 Hz</b>").arg(freq, 0, 'f', 1);
        else
            s = QStringLiteral("Corresponds to <b>%1 kHz</b>").arg(freq/1000.0, 0, 'f', 2);
        labExpFreq->setText(s);
    }

    void autoExposure()
    {
        if (props["Exp"] == 0) return;

        butAutoExp->setDisabled(true);
        edAutoExp->setDisabled(true);

        auto level = edAutoExp->value();
        if (level <= 0) level = 1;
        else if (level > 100) level = 100;
        edAutoExp->setValue(level);
        autoExpLevel = level / 100.0;
        qDebug() << LOG_ID << "Autoexposure" << autoExpLevel;

        Ori::Settings s;
        s.beginGroup("DeviceControl");
        s.setValue("autoExposurePercent", level);

        if (!setExpRaw(props["ExpMin"]))
            return;
        autoExp1 = props["Exp"];
        autoExp2 = 0;
        autoExpStep = 0;
        watingBrightness = true;
        peak->requestBrightness(this);
    }

    void autoExposureStep(double level)
    {
        qDebug() << LOG_ID << "Autoexposure step" << autoExpStep << "| exp" << props["Exp"] << "| level" << level;

        if (qAbs(level - autoExpLevel) < 0.01) {
            qDebug() << LOG_ID << "Autoexposure: stop(0)" << props["Exp"];
            goto stop;
        }

        if (level < autoExpLevel) {
            if (autoExp2 == 0) {
                if (!setExpRaw(qMin(autoExp1*2, props["ExpMax"])))
                    goto stop;
                // The above does not fail when setting higher-thah-max exposure
                // It just clamps it to max and the loop never ends.
                // So need an explicit check:
                if (props["Exp"] >= props["ExpMax"]) {
                    qDebug() << LOG_ID << "Autoexposure: underexposed" << props["Exp"];
                    Ori::Dlg::warning(tr("Underexposed"));
                    goto stop;
                }
                autoExp1 = props["Exp"];
            } else {
                autoExp1 = props["Exp"];
                if (!setExpRaw((autoExp1+autoExp2)/2))
                    goto stop;
                if (qAbs(autoExp1 - props["Exp"]) <= props["ExpStep"]) {
                    qDebug() << LOG_ID << "Autoexposure: stop(1)" << props["Exp"];
                    goto stop;
                }
            }
        } else {
            if (autoExp2 == 0) {
                if (props["Exp"] == props["ExpMin"]) {
                    qDebug() << LOG_ID << "Autoexposure: Overexposed";
                    Ori::Dlg::warning(tr("Overexposed"));
                    goto stop;
                }
                autoExp2 = autoExp1;
                autoExp1 = autoExp2/2;
                if (!setExpRaw((autoExp1+autoExp2)/2))
                    goto stop;
            } else {
                autoExp2 = props["Exp"];
                if (!setExpRaw((autoExp1+autoExp2)/2))
                    goto stop;
                if (qAbs(autoExp2 - props["Exp"]) <= props["ExpStep"]) {
                    qDebug() << LOG_ID << "Autoexposure: stop(2)" << props["Exp"];
                    goto stop;
                }
            }
        }
        autoExpStep++;
        watingBrightness = true;
        peak->requestBrightness(this);
        return;

    stop:
        butAutoExp->setDisabled(false);
        edAutoExp->setDisabled(false);
    }

    void applySettings()
    {
        auto &s = AppSettings::instance();
        propChangeWheelSm = 1 + double(s.propChangeWheelSm) / 100.0;
        propChangeWheelBig = 1 + double(s.propChangeWheelBig) / 100.0;
        propChangeArrowSm = 1 + double(s.propChangeArrowSm) / 100.0;
        propChangeArrowBig = 1 + double(s.propChangeArrowBig) / 100.0;

        Ori::Settings s1;
        s1.beginGroup("DeviceControl");
        edAutoExp->setValue(s1.value("autoExposurePercent", 80).toInt());
    }

    void settingsChanged() override
    {
        applySettings();
    }

    void setReadOnly(bool on) override
    {
        for (auto group : groups)
            group->setDisabled(on);
    }

    void toggleVertMirror(bool on)
    {
        auto res = vertMirrorIPL
            ? IDS.peak_IPL_Mirror_UpDown_Enable(hCam, on)
            : IDS.peak_Mirror_UpDown_Enable(hCam, on);
        if (PEAK_ERROR(res))
            Ori::Dlg::error(IDS.getPeakError(res));
        qDebug() << IDS.peak_IPL_Mirror_UpDown_IsEnabled(hCam);
    }

    void toggleHorzMirror(bool on)
    {
        auto res = horzMirrorIPL
            ? IDS.peak_IPL_Mirror_LeftRight_Enable(hCam, on)
            : IDS.peak_Mirror_LeftRight_Enable(hCam, on);
        if (PEAK_ERROR(res))
            Ori::Dlg::error(IDS.getPeakError(res));
    }

    PeakIntf *peak;
    peak_camera_handle hCam;
    CamPropEdit *edAutoExp;
    QList<QGroupBox*> groups;
    QPushButton *butAutoExp;
    QLabel *labExpFreq;
    QCheckBox *vertMirror, *horzMirror;
    bool vertMirrorIPL = false, horzMirrorIPL = false;
    QMap<const char*, double> props;
    double propChangeWheelSm, propChangeWheelBig;
    double propChangeArrowSm, propChangeArrowBig;
    double autoExpLevel, autoExp1, autoExp2;
    bool watingBrightness = false;
    bool closeRequested = false;
    int autoExpStep;

protected:
    void closeEvent(QCloseEvent *e) override
    {
        QWidget::closeEvent(e);
        // Event loop will crash if there is no event receiver anymore
        // Wait for the next brightness event and close after that
        if (watingBrightness) {
            closeRequested = true;
            e->ignore();
        }
    }

    bool event(QEvent *event) override
    {
        if (auto e = dynamic_cast<BrightEvent*>(event); e) {
            watingBrightness = false;
            if (closeRequested)
                close();
            else
                autoExposureStep(e->level);
            return true;
        }
        return QWidget::event(event);
    }
};

HardConfigPanel* IdsComfortCamera::hardConfgPanel(QWidget *parent)
{
    if (!_peak)
        return nullptr;
    if (!_configPanel)
        _configPanel = new IdsHardConfigPanel(_peak.get(), parent);
    return _configPanel;
}

#endif // WITH_IDS
