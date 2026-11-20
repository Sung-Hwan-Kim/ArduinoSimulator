#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

class ArduinoHighlighter : public QSyntaxHighlighter
{
public:
    explicit ArduinoHighlighter(QTextDocument* doc);
    void buildRules();   // 현재 스킨 모드 색으로 규칙 재구성
protected:
    void highlightBlock(const QString& text) override;
private:
    struct Rule { QRegularExpression re; QTextCharFormat fmt; };
    QVector<Rule> m_rules;
    QTextCharFormat m_comment;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    void setEditorFont(const QString& family, int size);
    void applyTheme();
    void markErrorLine(int line, const QString& msg);
    void clearErrorLine();
    void goToLine(int line);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaint(QPaintEvent* e);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void updateExtraSelections();
    QWidget* m_lineArea;
    ArduinoHighlighter* m_hl;
    int m_errorLine = 0;
};
