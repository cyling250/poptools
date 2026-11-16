#include "ClipboardCodec.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace poptools {

namespace ClipboardCodec {

namespace {

// 已知密钥前缀（大小写不敏感）。宁可漏打不多打：只列高置信度的
// 主流平台前缀，泛高熵规则兜底其余场景
const char *const kSecretPrefixes[] = {
    "sk-",        // OpenAI / 通用
    "skant-",     // Anthropic
    "ghp_", "gho_", "ghu_", "ghs_", "ghr_", "github_pat_", // GitHub
    "AKIA", "ASIA",                                        // AWS
    "xoxb", "xoxp", "xoxa", "xapp-",                       // Slack
    "glpat-",                                              // GitLab
    "shpat_", "shpss_",                                    // Shopify
    "AIza",                                                // Google API key
    "gsk_",                                                // Groq
    "sq0atp-", "sq0csp-",                                  // Square
    "eyJ",                                                 // JWT（base64 头固定开头）
};

// 密钥字符集 [A-Za-z0-9_\-+/=]
bool isSecretChar(QChar c)
{
    return (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
           || (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
           || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
           || c == QLatin1Char('_') || c == QLatin1Char('-')
           || c == QLatin1Char('+') || c == QLatin1Char('/')
           || c == QLatin1Char('=');
}

constexpr int kPrefixTailMin = 8; // 前缀后至少还要有这么多密钥字符
constexpr int kGenericMinLen = 20;
constexpr int kGenericDistinctMin = 10;

// 前缀尾段"全密钥字符"检查（T-062 S-3：maskEmbeddedPrefix 与 looksLikeSecret
// 原两处逐字重复）：tail 长度达标（≥kPrefixTailMin）且全部落在密钥字符集
bool tailAllSecret(QStringView tail)
{
    if (tail.size() < kPrefixTailMin)
        return false;
    for (QChar c : tail) {
        if (!isSecretChar(c))
            return false;
    }
    return true;
}

// Unicode 空白（QChar::isSpace 直通，留名以便语义检索）
bool isWhitespace(QChar c)
{
    return c.isSpace();
}

// T-047-FIX(P3-8③)：移入匿名命名空间——namespace 内非静态自由函数且未在
// 头声明，外部链接名是无谓的符号泄漏（与本文件其余内部函数一致）
// token 内嵌前缀（"key=sk-xxx" / "token:ghp_xxx" 一类无空白分隔的键值
// 场景）：只在 '=' / ':' / 引号之后认前缀——"task-" 里恰好含 "sk-" 这类
// 位置不构成边界，不误伤。命中则把前缀起的部分整体替换为 [masked]
QString maskEmbeddedPrefix(const QString &token)
{
    for (const char *p : kSecretPrefixes) {
        const QString prefix = QString::fromLatin1(p);
        int pos = 0;
        while ((pos = token.indexOf(prefix, pos, Qt::CaseInsensitive)) > 0) {
            const QChar prev = token.at(pos - 1);
            const bool boundary = prev == QLatin1Char('=') || prev == QLatin1Char(':')
                                  || prev == QLatin1Char('"')
                                  || prev == QLatin1Char('\'');
            const QStringView tail(token.constData() + pos + prefix.size(),
                                   token.size() - pos - prefix.size());
            if (boundary && tailAllSecret(tail))
                return token.left(pos) + QStringLiteral("[masked]");
            ++pos;
        }
    }
    return token;
}

} // namespace

bool looksLikeSecret(const QString &token)
{
    if (token.size() < kPrefixTailMin)
        return false;

    // 已知前缀：前缀 + ≥8 个密钥字符集字符
    for (const char *p : kSecretPrefixes) {
        const QString prefix = QString::fromLatin1(p);
        if (token.size() <= prefix.size())
            continue;
        if (!token.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        const QStringView tail(token.constData() + prefix.size(),
                               token.size() - prefix.size());
        if (tailAllSecret(tail))
            return true;
    }

    // 泛高熵：≥20 且全在密钥字符集 + 字母数字混合 + 去重字符 ≥10
    if (token.size() < kGenericMinLen)
        return false;
    bool hasLetter = false, hasDigit = false;
    QChar seen[kGenericDistinctMin];
    int distinct = 0;
    for (QChar c : token) {
        if (!isSecretChar(c))
            return false;
        if (c.isLetter())
            hasLetter = true;
        else if (c.isDigit())
            hasDigit = true;
        bool dup = false;
        for (int i = 0; i < distinct; ++i) {
            if (seen[i] == c) {
                dup = true;
                break;
            }
        }
        if (!dup && distinct < kGenericDistinctMin)
            seen[distinct++] = c;
    }
    return hasLetter && hasDigit && distinct >= kGenericDistinctMin;
}

QString maskSecrets(const QString &text)
{
    QString out;
    out.reserve(text.size());
    int i = 0;
    bool prevWasBearer = false;
    while (i < text.size()) {
        if (isWhitespace(text.at(i))) {
            out += text.at(i++);
            // 空白不复位 prevWasBearer：bearer 与其值之间隔着空格
            continue;
        }
        const int start = i;
        while (i < text.size() && !isWhitespace(text.at(i)))
            ++i;
        const QString token = text.mid(start, i - start);
        const bool bearerWord = token.compare(QLatin1String("bearer"),
                                              Qt::CaseInsensitive) == 0;
        if (looksLikeSecret(token) || prevWasBearer)
            out += QStringLiteral("[masked]");
        else
            out += maskEmbeddedPrefix(token);
        prevWasBearer = bearerWord;
    }
    return out;
}

QString contentHash(const QByteArray &headBytes, qint64 totalLen)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    // T-047-FIX(P3-4)：常量改名对齐字节语义（截断本就按 UTF-8 字节计）
    h.addData(headBytes.left(kHashHeadBytes));
    return QString::fromLatin1(h.result().toHex().left(16))
           + QLatin1Char(':') + QString::number(totalLen);
}

QJsonObject buildClipboardEvent(qint64 ts, const ClipboardPayload &p,
                                const QString &app, const QString &title)
{
    QJsonObject o;
    o.insert(QStringLiteral("ts"), ts);
    o.insert(QStringLiteral("app"), app);
    o.insert(QStringLiteral("title"), title);
    switch (p.kind) {
    case ClipboardPayload::Kind::Text:
        o.insert(QStringLiteral("kind"), QStringLiteral("text"));
        o.insert(QStringLiteral("chars"), static_cast<qint64>(p.chars));
        o.insert(QStringLiteral("hash"), p.hash);
        if (!p.head.isEmpty())
            o.insert(QStringLiteral("head"), maskSecrets(p.head));
        if (!p.tail.isEmpty())
            o.insert(QStringLiteral("tail"), maskSecrets(p.tail));
        break;
    case ClipboardPayload::Kind::Files:
        o.insert(QStringLiteral("kind"), QStringLiteral("files"));
        o.insert(QStringLiteral("hash"), p.hash);
        o.insert(QStringLiteral("paths"), QJsonArray::fromStringList(p.paths));
        if (p.pathsTruncated)
            o.insert(QStringLiteral("pathsTruncated"), true);
        break;
    case ClipboardPayload::Kind::Image:
        o.insert(QStringLiteral("kind"), QStringLiteral("image"));
        o.insert(QStringLiteral("hash"), p.hash);
        o.insert(QStringLiteral("width"), p.width);
        o.insert(QStringLiteral("height"), p.height);
        o.insert(QStringLiteral("bytes"), p.bytes);
        break;
    case ClipboardPayload::Kind::Empty:
        break;
    }
    return o;
}

ClipboardDeduper::Outcome ClipboardDeduper::observe(const QString &hash)
{
    Outcome o;
    if (hash.isEmpty())
        return o; // 无内容（读失败等）：不动状态
    if (hash == m_lastHash) {
        ++m_repeat; // 连续重复：只内存累计，不追加新行
        return o;
    }
    if (m_repeat > 0) {
        o.repeatHash = m_lastHash;
        o.repeatCount = m_repeat + 1; // 总次数 = 首条 entry + 累计重复
    }
    o.entry = true;
    m_lastHash = hash;
    m_repeat = 0;
    return o;
}

ClipboardDeduper::Outcome ClipboardDeduper::flush()
{
    Outcome o;
    if (m_repeat > 0 && !m_lastHash.isEmpty()) {
        o.repeatHash = m_lastHash;
        o.repeatCount = m_repeat + 1;
    }
    m_lastHash.clear();
    m_repeat = 0;
    return o;
}

} // namespace ClipboardCodec

} // namespace poptools
