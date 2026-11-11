#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QStringList>
#include <QTextBlock>
#include <QTextFormat>

namespace poptools {

// Qt markdown 导入器对 ``` 围栏块的落盘标记探针实测（Qt 6.8.3）：代码内容
// 行携带 BlockCodeLanguage（语言名），BlockCodeFence 属性并不落块（围栏行
// 本身被消费）。判定统一走本谓词——语法高亮与代码块复制按钮共用唯一口径。
inline bool isCodeFenceBlock(const QTextBlock &block)
{
    if (!block.isValid())
        return false;
    const QTextBlockFormat f = block.blockFormat();
    return f.intProperty(QTextFormat::BlockCodeFence) != 0
           || !f.stringProperty(QTextFormat::BlockCodeLanguage).isEmpty();
}

// ============================================================
// GoodyCodeHighlighter：代码块基础语法高亮（T-050-P2）。
//
// 挂在 assistant 气泡的 QTextDocument 上，setMarkdown 定稿后 Qt 的
// markdown 导入器为 ``` 围栏内容行标记 BlockCodeLanguage（探针实测见
// isCodeFenceBlock 头注）——highlightBlock 据此判定"该块是否走高亮"：
// 仅围栏内的行做 token 着色，围栏外正文不动。
// 规则表：C++ / Python / JS / JSON 的关键字 + 字符串 + 注释 + 数字，
// addLanguage 可扩展；语言别名归一（cpp→c++、py→python…）。
// 颜色随明暗主题切换：setSyntaxColors 后 rehighlight。
// ============================================================

struct CodeSyntaxColors
{
    QColor keyword;
    QColor string;
    QColor comment;
    QColor number;
    QColor punctuation; // 代码块内普通文字以外的弱色（暂保留扩展位）

    static CodeSyntaxColors light(); // 浅色主题取值
    static CodeSyntaxColors dark();  // 深色主题取值
};

class GoodyCodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit GoodyCodeHighlighter(QTextDocument *document);

    void setSyntaxColors(const CodeSyntaxColors &colors); // 换主题后调用
    // 注册/覆盖一门语言的高亮规则（name 为 ``` 围栏语言标记，小写）
    void addLanguage(const QString &name, const QStringList &keywords,
                     bool hashComments, bool slashComments);

protected:
    void highlightBlock(const QString &text) override;

private:
    // 单行 token 着色：字符串与注释左→右谁先出现谁占区间（T-050-FIX2），
    // 关键字/数字跳过已占区间
    void highlightCodeLine(const QString &text,
                           const QList<QRegularExpression> &keywordRes,
                           bool hashComments, bool slashComments);

    struct TokenRange
    {
        int start = 0;
        int length = 0;
        QColor color;
    };

    CodeSyntaxColors m_colors;
    QHash<QString, int> m_langIndex; // 归一语言名 → m_rules 下标
    struct LangRule
    {
        QList<QRegularExpression> keywordRes; // T-050-FIX2：预编译关键字正则（注册时一次构建，复用）
        bool hashComments = false;
        bool slashComments = false;
    };
    QList<LangRule> m_rules;

    QRegularExpression m_stringRe;  // "…" / '…' / `…`（含转义）
    QRegularExpression m_numberRe;
};

} // namespace poptools
