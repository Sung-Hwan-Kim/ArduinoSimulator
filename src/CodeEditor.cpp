#include "CodeEditor.h"
#include "Theme.h"
#include <QPainter>
#include <QTextBlock>
#include <QToolTip>

// ---------------------------------------------------------------------------
ArduinoHighlighter::ArduinoHighlighter(QTextDocument* doc)
    : QSyntaxHighlighter(doc)
{
    buildRules();
}

void ArduinoHighlighter::buildRules()
{
    m_rules.clear();
    auto fmt = [](const QColor& c, bool bold = false) { QTextCharFormat f; f.setForeground(c); if (bold) f.setFontWeight(QFont::Bold); return f; };
    const QStringList types = { "void", "int", "long", "unsigned", "float", "double", "char", "byte", "bool", "boolean", "short", "word",
                                "String", "const", "static", "volatile", "uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "size_t", "struct", "enum" };
    const QStringList kws = { "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "true", "false", "sizeof", "NULL", "nullptr" };
    const QStringList consts = { "HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
                                 "CHANGE", "RISING", "FALLING", "PI", "DEC", "HEX", "BIN", "OCT", "LSBFIRST", "MSBFIRST" };
    const QStringList funcs = { "setup", "loop", "pinMode", "digitalWrite", "digitalRead", "analogRead", "analogWrite", "delay", "delayMicroseconds",
                                "millis", "micros", "Serial", "tone", "noTone", "pulseIn", "map", "constrain", "min", "max", "abs", "random",
                                "attachInterrupt", "digitalPinToInterrupt", "shiftOut", "Wire", "EEPROM" };
    for (const QString& w : types) m_rules.push_back({ QRegularExpression("\\b" + w + "\\b"), fmt(Theme::synType(), true) });
    for (const QString& w : kws) m_rules.push_back({ QRegularExpression("\\b" + w + "\\b"), fmt(Theme::synKeyword(), true) });
    for (const QString& w : consts) m_rules.push_back({ QRegularExpression("\\b" + w + "\\b"), fmt(Theme::synConst()) });
    for (const QString& w : funcs) m_rules.push_back({ QRegularExpression("\\b" + w + "\\b"), fmt(Theme::synFunc()) });
    m_rules.push_back({ QRegularExpression("\\b[0-9]+(\\.[0-9]+)?[fFuUlL]*\\b|\\b0x[0-9A-Fa-f]+\\b"), fmt(Theme::synNumber()) });
    m_rules.push_back({ QRegularExpression("^\\s*#\\s*\\w+.*"), fmt(Theme::synPreproc()) });
    m_rules.push_back({ QRegularExpression("\"([^\"\\\\]|\\\\.)*\""), fmt(Theme::synString()) });
    m_rules.push_back({ QRegularExpression("'([^'\\\\]|\\\\.)*'"), fmt(Theme::synString()) });
    m_comment = fmt(Theme::synComment());
    m_comment.setFontItalic(true);
}

void CodeEditor::applyTheme()
{
    m_hl->buildRules();
    m_hl->rehighlight();
    updateExtraSelections();
    m_lineArea->update();
    viewport()->update();
}

void ArduinoHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& r : m_rules) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), r.fmt); }
    }
    // 한 줄 주석
    static const QRegularExpression lineCmt("//[^\n]*");
    auto m = lineCmt.match(text);
    if (m.hasMatch()) {
        // 문자열 안의 // 는 대충 무시
        int q = 0;
        for (int i = 0; i < m.capturedStart(); ++i) if (text[i] == '"') ++q;
        if (q % 2 == 0) setFormat(m.capturedStart(), m.capturedLength(), m_comment);
    }
    // 블록 주석
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : (int)text.indexOf("/*");
    while (start >= 0) {
        int end = (int)text.indexOf("*/", start);
        int len;
        if (end < 0) { setCurrentBlockState(1); len = (int)text.length() - start; }
        else len = end - start + 2;
        setFormat(start, len, m_comment);
        start = (int)text.indexOf("/*", start + len);
    }
}

// ---------------------------------------------------------------------------
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* e) : QWidget(e), m_editor(e) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* e) override { m_editor->lineNumberAreaPaint(e); }
private:
    CodeEditor* m_editor;
};

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    m_lineArea = new LineNumberArea(this);
    m_hl = new ArduinoHighlighter(document());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setEditorFont("Consolas", 11);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy) m_lineArea->scroll(0, dy);
        else m_lineArea->update(0, r.y(), m_lineArea->width(), r.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateExtraSelections);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { if (m_errorLine) clearErrorLine(); });
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    updateExtraSelections();
}

void CodeEditor::setEditorFont(const QString& family, int size)
{
    QFont f(family);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(size);
    setFont(f);
    setTabStopDistance(QFontMetricsF(f).horizontalAdvance(' ') * 2);
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = QString::number(qMax(1, blockCount())).size();
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_lineArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaint(QPaintEvent* e)
{
    QPainter p(m_lineArea);
    p.fillRect(e->rect(), Theme::editorGutterBg());
    QTextBlock block = firstVisibleBlock();
    int num = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            bool err = (num + 1) == m_errorLine;
            if (err) p.fillRect(0, top, m_lineArea->width(), fontMetrics().height(), Theme::editorErrorBg());
            p.setPen(err ? QColor(255, 90, 90) : Theme::editorGutterText());
            p.drawText(0, top, m_lineArea->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(num + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++num;
    }
}

void CodeEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> sels;
    QTextEdit::ExtraSelection cur;
    cur.format.setBackground(Theme::editorCurrentLine());
    cur.format.setProperty(QTextFormat::FullWidthSelection, true);
    cur.cursor = textCursor();
    cur.cursor.clearSelection();
    sels << cur;
    if (m_errorLine > 0) {
        QTextBlock b = document()->findBlockByNumber(m_errorLine - 1);
        if (b.isValid()) {
            QTextEdit::ExtraSelection es;
            es.format.setBackground(Theme::editorErrorBg());
            es.format.setProperty(QTextFormat::FullWidthSelection, true);
            es.cursor = QTextCursor(b);
            sels << es;
        }
    }
    setExtraSelections(sels);
}

void CodeEditor::markErrorLine(int line, const QString& msg)
{
    m_errorLine = line;
    updateExtraSelections();
    m_lineArea->update();
    if (line > 0) goToLine(line);
    if (!msg.isEmpty()) setToolTip(msg);
}

void CodeEditor::clearErrorLine()
{
    m_errorLine = 0;
    setToolTip(QString());
    updateExtraSelections();
    m_lineArea->update();
}

void CodeEditor::goToLine(int line)
{
    QTextBlock b = document()->findBlockByNumber(qMax(0, line - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
}

void CodeEditor::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Tab && !(e->modifiers() & Qt::ShiftModifier)) {
        insertPlainText("  ");
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        QString line = textCursor().block().text();
        QString ind;
        for (QChar c : line) { if (c == ' ' || c == '\t') ind += c; else break; }
        if (line.trimmed().endsWith('{')) ind += "  ";
        QPlainTextEdit::keyPressEvent(e);
        insertPlainText(ind);
        return;
    }
    if (e->key() == Qt::Key_BraceRight) {
        QTextCursor c = textCursor();
        QString line = c.block().text();
        if (line.trimmed().isEmpty() && line.size() >= 2) {
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            if (c.selectedText() == "  ") c.removeSelectedText();
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}
