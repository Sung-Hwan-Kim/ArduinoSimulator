#include "AppSettings.h"
#include "Catalog.h"
#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArduinoSimulator"));
    // Qt 가 모든 창 제목 끝에 " - 표시 이름" 을 붙임 → 버전 포함
    QApplication::setApplicationDisplayName(QStringLiteral("아두이노 시뮬레이터 v" APP_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral("ArduinoSimulator"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION_STR));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    QFont f(QStringLiteral("Malgun Gothic"));
    f.setPointSize(9);
    QApplication::setFont(f);

    AppSettings::instance().load();
    Catalog::setCustomParts(AppSettings::instance().customParts);

    // 명령줄 옵션 (자동 테스트용): --example N --run --screenshot 파일 --seconds S --parts id,id,...
    //                               --geometry x,y,w,h --theme dark|light
    const QStringList args = app.arguments();
    int example = -1, seconds = 3;
    bool run = false;
    QString shot, parts, geometry, theme = AppSettings::instance().theme;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--example" && i + 1 < args.size()) example = args[++i].toInt() - 1;
        else if (args[i] == "--run") run = true;
        else if (args[i] == "--screenshot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--seconds" && i + 1 < args.size()) seconds = args[++i].toInt();
        else if (args[i] == "--parts" && i + 1 < args.size()) parts = args[++i];
        else if (args[i] == "--geometry" && i + 1 < args.size()) geometry = args[++i];
        else if (args[i] == "--theme" && i + 1 < args.size()) theme = args[++i];
    }

    // 스킨 모드 (기본: 다크)
    Theme::apply(theme);

    MainWindow w;
    w.show();
    w.startup();

    const QStringList g = geometry.split(',');
    if (g.size() == 4) {
        // 테스트용 창 위치 (창 테두리 포함, INI 에 저장하지 않음)
        w.showNormal();
        w.resize(g[2].toInt() - 16, g[3].toInt() - 39);
        w.move(g[0].toInt(), g[1].toInt());
    }
    if (example >= 0 || !shot.isEmpty() || !parts.isEmpty()) w.selfTest(example, run, shot, seconds, parts);
    return app.exec();
}
