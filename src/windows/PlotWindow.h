#ifndef PLOT_WINDOW_H
#define PLOT_WINDOW_H

#include <QMainWindow>
#include <QPointer>

class QAction;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;

class Camera;
struct CameraStats;
class HardConfigPanel;
class MeasureProgressBar;
class MeasureSaver;
class Plot;
class PlotIntf;
class TableIntf;

namespace Ori {
class MruFileList;
namespace Widgets {
class StatusBar;
}}

class PlotWindow : public QMainWindow
{
    Q_OBJECT

public:
    PlotWindow(QWidget *parent = nullptr);
    ~PlotWindow();

signals:
    void camConfigChanged();

protected:
    void closeEvent(class QCloseEvent*) override;
    void changeEvent(QEvent* e) override;
    bool event(QEvent *event) override;

private:
    Plot *_plot;
    QSharedPointer<Camera> _camera;
    QSharedPointer<MeasureSaver> _saver;
    QAction *_actionMeasure, *_actionOpenImg, *_actionCamConfig,
        *_actionBeamInfo, *_actionLoadColorMap, *_actionCleanColorMaps,
        *_actionEditRoi, *_actionUseRoi, *_actionZoomFull, *_actionZoomRoi,
        *_actionCamWelcome, *_actionCamImage, *_actionCamDemo, *_actionRefreshCams,
        *_actionHardConfig, *_actionSaveRaw, *_actionRawView,
        *_actionCrosshairsShow, *_actionCrosshairsEdit;
    QAction *_buttonMeasure, *_buttonOpenImg;
    QActionGroup *_colorMapActions;
    QTableWidget *_table;
    QTableWidgetItem *_itemRenderTime, *_itemErrCount;
    TableIntf *_tableIntf;
    PlotIntf *_plotIntf;
    Ori::MruFileList *_mru;
    Ori::Widgets::StatusBar *_statusBar;
    QMenu *_camSelectMenu, *_colorMapMenu;
    QToolButton *_buttonSelectCam;
    QString _prevImage;
    MeasureProgressBar *_measureProgress;
    QDockWidget *_hardConfigDock;
    HardConfigPanel *_stubConfigPanel = nullptr;
    HardConfigPanel *_camConfigPanel = nullptr;

    void createMenuBar();
    void createToolBar();
    void createStatusBar();
    void createDockPanel();
    void createPlot();
    void restoreState();
    void storeState();
    void fillCamSelector();

    void newWindow();
    void openImageDlg();
    void toggleMeasure(bool force);
    void stopCapture();
    void editCamConfig(int pageId = -1);
    void activateCamWelcome();
    void activateCamImage();
    void activateCamDemo();
#ifdef WITH_IDS
    void activateCamIds();
#endif
    void toggleHardConfig();
    void updateHardConfgPanel();
    void updateViewMenu();
    void updateColorMapMenu();
    void selectColorMapFile();

    void statsReceived(const CameraStats &stats);
    void captureStopped();
    void dataReady();
    void roiEdited();
    void configChanged();

    void openImage(const QString& fileName);
    void updateActions();
    void updateThemeColors();
    void setThemeColors();
    void showFps(int fps);
    void showCamConfig(bool replot);
    void processImage();
    void toggleRoi();
    void toggleRawView();
    void toggleCrosshairsEditing();
    void toggleCrosshairsVisbility();
};

#endif // PLOT_WINDOW_H
