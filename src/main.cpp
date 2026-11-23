#include "AppSettings.h"
#include "Catalog.h"
#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

#ifdef Q_OS_MACOS
#include <QHash>
#include <QTranslator>

// macOS 앱 메뉴(서비스, 가리기, 종료 등)는 Qt 가 영어 문구로 만듦 → 한글로 바꿔 줌
class MacAppMenuTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* sourceText, const char* = nullptr, int = -1) const override
    {
        if (qstrcmp(context, "MAC_APPLICATION_MENU") != 0 || !sourceText) return {};
        static const QHash<QString, QString> ko = {
            { QStringLiteral("Services"), QStringLiteral("서비스") },
            { QStringLiteral("Hide %1"), QStringLiteral("%1 가리기") },
            { QStringLiteral("Hide Others"), QStringLiteral("기타 가리기") },
            { QStringLiteral("Show All"), QStringLiteral("모두 보기") },
            { QStringLiteral("Preferences..."), QStringLiteral("환경설정...") },
            { QStringLiteral("Quit %1"), QStringLiteral("%1 종료") },
            { QStringLiteral("About %1"), QStringLiteral("%1 정보") },
        };
        return ko.value(QString::fromLatin1(sourceText));
    }
};
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArduinoSimulator"));
    // Qt 가 모든 창 제목 끝에 " - 표시 이름" 을 붙임 → 버전 포함
    QApplication::setApplicationDisplayName(QStringLiteral("아두이노 시뮬레이터 v" APP_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral("ArduinoSimulator"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION_STR));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

#ifdef _WIN32
    QApplication::setFont(Theme::uiFont(9));
#elif defined(Q_OS_MACOS)
    // macOS: 시스템 글꼴(한글은 Apple SD 산돌고딕 Neo 로 자동 대체) 그대로 사용
    static MacAppMenuTranslator macMenuKo;
    QApplication::installTranslator(&macMenuKo);
#else
    QApplication::setFont(Theme::uiFont(10));
#endif

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
