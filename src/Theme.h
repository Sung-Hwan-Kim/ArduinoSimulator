#pragma once
// 스킨 모드 (다크 / 라이트) - 팔레트와 캔버스/편집기 색상
#include <QColor>
#include <QFont>
#include <QString>

namespace Theme {

// mode: "dark" | "light"
void apply(const QString& mode);
bool dark();

// 회로 캔버스
QColor canvasBg();
QColor gridDot();
QColor gridMajor();
QColor partLabel();      // 부품 참조기호/값 라벨
QColor accentText();     // 서보 각도, 거리 표시 등
QColor wireHalo();       // 어두운 전선이 배경에 묻히지 않도록 테두리

// 코드 편집기
QColor editorCurrentLine();
QColor editorGutterBg();
QColor editorGutterText();
QColor editorErrorBg();
QColor synType();
QColor synKeyword();
QColor synConst();
QColor synFunc();
QColor synNumber();
QColor synPreproc();
QColor synString();
QColor synComment();

// 일반 안내 문구
QColor hintText();

// 글꼴 (Windows: 맑은 고딕/Consolas, Linux: Noto CJK/DejaVu 로 자동 대체)
QFont uiFont(int pointSize);
QFont monoFont(int pointSize, const QString& preferred = QString());
QFont pixelFont(int pixelSize, bool bold = false);

} // namespace Theme
