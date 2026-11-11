#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace poptools {

// ============================================================
// 剪贴板流纯逻辑层（T-047-CLIP）：打码 / 哈希 / 事件构造 / 去重。
// 全部为纯函数/纯状态机，无 Win32 依赖，单测直接覆盖；真实剪贴板
// 读取（OpenClipboard/GetClipboardData）在 ClipboardListener 内，
// 不进单测。
// ============================================================
namespace ClipboardCodec {

// ---- 高熵密钥打码（宁可漏打不多打） ----

// 单 token 是否像密钥：
//  * 命中已知前缀（sk-/ghp_/AKIA/xoxb/AIza/eyJ…，大小写不敏感）且
//    前缀后还有 ≥8 个密钥字符集 [A-Za-z0-9_\-+/=]；
//  * 或泛高熵：长度 ≥20、全部落在密钥字符集、同时含字母与数字、
//    去重字符 ≥10（排除纯数字单号/重复串；UUID 因 32 位十六进制
//    惯例一并保守打码）。
bool looksLikeSecret(const QString &token);

// 把文本中的密钥 token 替换为 [masked]（逐空白分隔 token 判定；
// 上一 token 为 "bearer" 时当前 token 一并打码——HTTP 头场景）
QString maskSecrets(const QString &text);

// ---- 内容哈希（去重键） ----

// 只算前 kHashHeadBytes 字节（UTF-8 编码字节，T-047-FIX(P3-4) 常量名对齐
// 字节语义）的 SHA-256（截 16 hex）+ 总长：大文本不做全文哈希，worker 侧
// 开销有界
constexpr int kHashHeadBytes = 65536;
QString contentHash(const QByteArray &headBytes, qint64 totalLen);

// ---- 剪贴板载荷与事件行 ----

// ClipboardListener 在 worker 线程提取的原始载荷（文本已切片，
// 不携带全文——大文本零整块拷贝）
struct ClipboardPayload
{
    enum class Kind { Empty, Text, Files, Image };

    Kind kind = Kind::Empty;
    // Text：chars=总字符数；head/tail=头尾各 ≤200 原始字符（未打码）
    qint64 chars = 0;
    QString hash;
    QString head, tail;
    // Files：路径 ≤5 条（超出截断）
    QStringList paths;
    bool pathsTruncated = false;
    // Image：只记元数据，不落内容
    int width = 0, height = 0;
    qint64 bytes = 0;
};

// 事件行（clipboard.jsonl 的一行）：公共 ts/app/title/kind + 按 kind
// 的字段；head/tail 过 maskSecrets 后落盘
QJsonObject buildClipboardEvent(qint64 ts, const ClipboardPayload &p,
                                const QString &app, const QString &title);

// ---- 去重与 repeat（纯状态机，内存累计） ----

// 同一 hash 连续重复复制不追加新行，只内存累计；内容变化时先冲账一行
// {"kind":"repeat","hash":H,"count":N}（N=该内容连续复制的总次数，含
// 首次——首条 entry 已落盘，repeat 行补足总量），再写新条目。
// 停止/收尾时 flush() 补记尚未冲账的 repeat。
class ClipboardDeduper
{
public:
    struct Outcome
    {
        QString repeatHash;  // 非空 → 先写 repeat 行（hash=上一内容）
        int repeatCount = 0; // repeatCount = 上一内容连续总次数（≥2）
        bool entry = false;  // 追加新条目
    };

    Outcome observe(const QString &hash);
    Outcome flush(); // 无挂起 repeat 时返回无效 Outcome 并复位

    QString lastHash() const { return m_lastHash; }
    int pendingRepeatCount() const { return m_repeat; }

private:
    QString m_lastHash;
    int m_repeat = 0; // 最近内容的额外重复次数（不含首条 entry）
};

} // namespace ClipboardCodec

} // namespace poptools
