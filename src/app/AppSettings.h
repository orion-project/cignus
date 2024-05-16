#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "core/OriTemplates.h"

#include <QObject>

class IAppSettingsListener
{
public:
    IAppSettingsListener();
    virtual ~IAppSettingsListener();

    virtual void settingsChanged() {}
};

class AppSettings : public QObject, public Ori::Notifier<IAppSettingsListener>
{
public:
    static AppSettings& instance();

    AppSettings();

    int propChangeWheelSm = 20;
    int propChangeWheelBig = 100;
    int propChangeArrowSm = 20;
    int propChangeArrowBig = 100;
#ifdef WITH_IDS
    bool idsEnabled;
    QString idsSdkDir;
#endif

    bool isDevMode = false;

    enum ConfigPages {
        cfgDev,
    #ifdef WITH_IDS
        cfgIds,
    #endif
    };

    void load();
    void save();
    bool edit();
};

#endif // APP_SETTINGS_H
