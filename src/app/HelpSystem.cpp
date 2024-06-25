#include "HelpSystem.h"

#include "helpers/OriLayouts.h"
#include "widgets/OriLabels.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QUrl>

using namespace Ori::Layouts;
using Ori::Widgets::Label;

namespace {

HelpSystem *__instance = nullptr;

static QString homePageUrl() { return "https://github.com/orion-project/beam-inspector"; }
static QString newIssueUrl() { return "https://github.com/orion-project/beam-inspector/issues/new"; }

} // namespace

HelpSystem::HelpSystem() : QObject()
{
}

HelpSystem* HelpSystem::instance()
{
    if (!__instance)
        __instance = new HelpSystem();
    return __instance;
}

QString HelpSystem::appVersion()
{
    return QString("%1.%2.%3").arg(APP_VER_MAJOR).arg(APP_VER_MINOR).arg(APP_VER_PATCH);
}

void HelpSystem::visitHomePage()
{
    QDesktopServices::openUrl(QUrl(homePageUrl()));
}

void HelpSystem::sendBugReport()
{
    QDesktopServices::openUrl(QUrl(newIssueUrl()));
}

void HelpSystem::showAbout()
{
    auto w = new QDialog;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setWindowTitle(tr("About %1").arg(qApp->applicationName()));
    QSize sz(800, 500);
    w->setMaximumSize(sz);
    w->setMinimumSize(sz);
    w->resize(sz);

    auto p = w->palette();
    p.setBrush(QPalette::Window, QBrush(QPixmap(":/misc/about_bg")));
    w->setPalette(p);

    auto labelLogo = new QLabel;
    labelLogo->setPixmap(QIcon(":/logos/main").pixmap(500));

    auto labelVersion = new QLabel("0.0.10");
    labelVersion->setStyleSheet("font-weight:bold;font-size:50pt;color:#333344;padding-left:-40px");
    labelVersion->setAlignment(Qt::AlignRight);

    auto labelGit = new Label;
    labelGit->setPixmap(QIcon(":/logos/github").pixmap(60));
    labelGit->setCursor(Qt::PointingHandCursor);
    labelGit->setToolTip(homePageUrl());
    connect(labelGit, &Label::clicked, this, [this]{ visitHomePage(); });

    auto labelQt = new Label;
    labelQt->setPixmap(QIcon(":/logos/qt").pixmap(60));
    labelQt->setCursor(Qt::PointingHandCursor);
    labelQt->setToolTip(tr("About Qt"));
    connect(labelQt, &Label::clicked, this, []{ qApp->aboutQt(); });

    auto labelSupport1 = new Label;
    labelSupport1->setPixmap(QPixmap(":/logos/n2-photonics"));
    labelSupport1->setCursor(Qt::PointingHandCursor);
    labelSupport1->setToolTip("https://www.n2-photonics.de");
    connect(labelSupport1, &Label::clicked, this, []{ QDesktopServices::openUrl(QUrl("https://www.n2-photonics.de")); });

    LayoutH({
        labelLogo,
        LayoutV({
            labelVersion,
            Stretch(),
            LayoutH({Stretch(), labelGit}).setMargin(0),
            LayoutH({Stretch(), labelQt}).setMargin(0),
            Stretch(),
            LayoutH({Stretch(), labelSupport1}).setMargin(0),
        }).setMargins(0, 0, 24, 16),
    }).setMargin(0).useFor(w);

    w->exec();
}
