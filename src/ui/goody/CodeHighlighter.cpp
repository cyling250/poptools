#include "CodeHighlighter.h"
// T-050-P2：代码块语法高亮实现（规则表/颜色随主题；判定口径见 CodeHighlighter.h
// isCodeFenceBlock 头注——Qt 6.8.3 围栏块只落 BlockCodeLanguage）

#include <QStringList>

namespace poptools {

CodeSyntaxColors CodeSyntaxColors::light()
{
    CodeSyntaxColors c;
    c.keyword = QColor(0x1A, 0x5C, 0xDB);  // 主色深阶：关键字
    c.string = QColor(0x1E, 0x8E, 0x3E);   // 绿：字符串
    c.comment = QColor(0x8A, 0x8A, 0x8A);  // 灰：注释（代码块内惯例，非 UI 灰字）
    c.number = QColor(0xB7, 0x5C, 0x0E);   // 橙棕：数字
    c.punctuation = QColor(0x00, 0x00, 0x00);
    return c;
}

CodeSyntaxColors CodeSyntaxColors::dark()
{
    CodeSyntaxColors c;
    c.keyword = QColor(0x6C, 0xB6, 0xFF);  // 亮蓝：关键字
    c.string = QColor(0x7E, 0xC6, 0x99);   // 绿：字符串
    c.comment = QColor(0x7A, 0x7A, 0x7A);  // 灰：注释
    c.number = QColor(0xD1, 0x9A, 0x66);   // 橙：数字
    c.punctuation = QColor(0xFF, 0xFF, 0xFF);
    return c;
}

GoodyCodeHighlighter::GoodyCodeHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    // 字符串：单引号/双引号/反引号，含 \" 转义（非贪婪到最近的闭引号）
    m_stringRe.setPattern(QStringLiteral(
        "\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'|`(?:\\\\.|[^`\\\\])*`"));
    // 数字（含小数；不碰单词中间）
    m_numberRe.setPattern(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));

    // ---- 规则表（可扩展：addLanguage 注册/覆盖） ----
    addLanguage(QStringLiteral("c++"),
                QStringList{"alignas", "alignof", "auto", "bool", "break", "case",
                            "catch", "char", "class", "const", "constexpr",
                            "continue", "default", "delete", "do", "double",
                            "else", "enum", "explicit", "export", "extern",
                            "false", "float", "for", "friend", "goto", "if",
                            "inline", "int", "long", "mutable", "namespace",
                            "new", "noexcept", "nullptr", "operator", "private",
                            "protected", "public", "return", "short", "signed",
                            "sizeof", "static", "struct", "switch", "template",
                            "this", "throw", "true", "try", "typedef", "typename",
                            "union", "unsigned", "using", "virtual", "void",
                            "volatile", "while"},
                false, true);
    addLanguage(QStringLiteral("python"),
                QStringList{"and", "as", "assert", "async", "await", "break",
                            "class", "continue", "def", "del", "elif", "else",
                            "except", "False", "finally", "for", "from",
                            "global", "if", "import", "in", "is", "lambda",
                            "None", "nonlocal", "not", "or", "pass", "raise",
                            "return", "True", "try", "while", "with", "yield"},
                true, false);
    addLanguage(QStringLiteral("javascript"),
                QStringList{"async", "await", "break", "case", "catch", "class",
                            "const", "continue", "default", "delete", "do",
                            "else", "export", "extends", "false", "finally",
                            "for", "function", "if", "import", "in", "instanceof",
                            "let", "new", "null", "of", "return", "static",
                            "super", "switch", "this", "throw", "true", "try",
                            "typeof", "undefined", "var", "void", "while", "yield"},
                false, true);
    addLanguage(QStringLiteral("json"), QStringList{"false", "null", "true"},
                false, false);

    // 语言别名映射（c 家族 / python / js-ts 简写指回同一份规则）
    for (const QString &alias : {QStringLiteral("cpp"), QStringLiteral("cc"),
                                 QStringLiteral("cxx"), QStringLiteral("c")})
        m_langIndex.insert(alias, m_langIndex.value(QStringLiteral("c++")));
    for (const QString &alias : {QStringLiteral("py"), QStringLiteral("python3")})
        m_langIndex.insert(alias, m_langIndex.value(QStringLiteral("python")));
    for (const QString &alias : {QStringLiteral("js"), QStringLiteral("node"),
                                 QStringLiteral("ts"), QStringLiteral("typescript")})
        m_langIndex.insert(alias, m_langIndex.value(QStringLiteral("javascript")));
}

void GoodyCodeHighlighter::addLanguage(const QString &name, const QStringList &keywords,
                                       bool hashComments, bool slashComments)
{
    const QString key = name.toLower();
    const int idx = m_rules.size();
    LangRule rule;
    rule.hashComments = hashComments;
    rule.slashComments = slashComments;
    // T-050-FIX2：关键字正则按语言预编译缓存——原先每行每关键字现场编译
    //（60 关键字 × 每行），现在注册时构建一次，highlightBlock 直接复用
    for (const QString &kw : keywords) {
        if (kw.isEmpty())
            continue;
        rule.keywordRes.append(QRegularExpression(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(kw))));
    }
    m_rules.append(rule);
    m_langIndex.insert(key, idx);
}

void GoodyCodeHighlighter::setSyntaxColors(const CodeSyntaxColors &colors)
{
    m_colors = colors;
    rehighlight();
}

void GoodyCodeHighlighter::highlightBlock(const QString &text)
{
    // 代码块判定（isCodeFenceBlock 唯一口径，见头注）：Qt markdown 导入器
    // 为围栏内容行标记 BlockCodeLanguage（6.8.3 实测不落 BlockCodeFence）；
    // 围栏外正文不做任何着色
    const QTextBlockFormat fmt = currentBlock().blockFormat();
    if (!isCodeFenceBlock(currentBlock()))
        return;
    // 围栏行本身（```xxx / ```）不着色，只着色代码内容行
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("```")))
        return;

    const QString lang = fmt.stringProperty(QTextFormat::BlockCodeLanguage).toLower();
    const int idx = m_langIndex.value(lang, -1);
    if (idx < 0 || idx >= m_rules.size())
        return; // 未注册语言：不高亮（纯文本呈现，不瞎猜）
    const LangRule &rule = m_rules.at(idx);
    highlightCodeLine(text, rule.keywordRes, rule.hashComments, rule.slashComments);
}

void GoodyCodeHighlighter::highlightCodeLine(const QString &text,
                                             const QList<QRegularExpression> &keywordRes,
                                             bool hashComments, bool slashComments)
{
    QList<TokenRange> tokens;

    const auto addRange = [&tokens](int start, int length, const QColor &color) {
        if (length > 0)
            tokens.append(TokenRange{start, length, color});
    };
    // 区间重叠判定（关键字/数字跳过已占区间）
    const auto overlaps = [&tokens](int start, int length) {
        for (const TokenRange &t : tokens) {
            if (start < t.start + t.length && t.start < start + length)
                return true;
        }
        return false;
    };

    // T-050-FIX2：字符串与注释左→右单遍判定——谁先出现谁占区间，扫到区间
    // 末尾继续。原先注释先于字符串判定，python 行内字符串里的 `#` 被误判
    // 成注释；简单对调又会反噬（C++ 注释里的引号吞掉整条 `//` 注释），
    // 故按起始位置裁决。同块跨行的 /* */ 基础版仍按单行处理
    QRegularExpressionMatchIterator strIt = m_stringRe.globalMatch(text);
    QRegularExpressionMatch nextStr = strIt.hasNext() ? strIt.next()
                                                     : QRegularExpressionMatch{};
    int cursor = 0;
    while (cursor < text.size()) {
        while (nextStr.hasMatch() && nextStr.capturedStart() < cursor)
            nextStr = strIt.hasNext() ? strIt.next() : QRegularExpressionMatch{};
        const int strStart = nextStr.hasMatch() ? nextStr.capturedStart() : -1;
        int cStart = -1;
        if (hashComments) {
            const int p = text.indexOf(QLatin1Char('#'), cursor);
            if (p >= 0 && (cStart < 0 || p < cStart))
                cStart = p;
        }
        if (slashComments) {
            const int p = text.indexOf(QStringLiteral("//"), cursor);
            if (p >= 0 && (cStart < 0 || p < cStart))
                cStart = p;
        }
        if (strStart < 0 && cStart < 0)
            break; // 行内再无字符串/注释
        if (cStart >= 0 && (strStart < 0 || cStart < strStart)) {
            addRange(cStart, text.size() - cStart, m_colors.comment);
            break; // 注释到行尾，扫线结束
        }
        addRange(strStart, nextStr.capturedLength(), m_colors.string);
        cursor = strStart + nextStr.capturedLength();
    }

    // 关键字（词边界；正则已按语言预编译缓存，T-050-FIX2）
    for (const QRegularExpression &re : keywordRes) {
        auto kwIt = re.globalMatch(text);
        while (kwIt.hasNext()) {
            const auto m = kwIt.next();
            if (!overlaps(m.capturedStart(), m.capturedLength()))
                addRange(m.capturedStart(), m.capturedLength(), m_colors.keyword);
        }
    }

    // 数字
    QRegularExpressionMatchIterator it = m_numberRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!overlaps(m.capturedStart(), m.capturedLength()))
            addRange(m.capturedStart(), m.capturedLength(), m_colors.number);
    }

    for (const TokenRange &t : tokens) {
        QTextCharFormat f;
        f.setForeground(t.color);
        setFormat(t.start, t.length, f);
    }
}

} // namespace poptools
