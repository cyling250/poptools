#include "Translator.h"
#include "core/settings/Settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace poptools {

namespace {

// 专业翻译/解释/总结/改写提示词统一在 Translator.h 的纯函数
// translateActionPrompt()（可测、双端共用），此处不再重复实现。

// 用户在设置中配置的翻译领域与专有名词术语表 → 追加到系统提示词。
// 两项均未配置时返回空串，提示词保持最简。
QString buildDomainAndTermsPrompt()
{
    const Settings &s = Settings::instance();
    QStringList blocks;

    const QStringList domains = s.translateDomains();
    if (!domains.isEmpty()) {
        blocks += QStringLiteral(
                      "The text belongs to the following subject domains: %1. Use the standard "
                      "terminology and writing conventions of these domains.")
                      .arg(domains.join(QStringLiteral(", ")));
    }

    const QList<Settings::TermPair> terms = s.translateTerms();
    if (!terms.isEmpty()) {
        QString t = QStringLiteral(
            "Terminology mapping — when these source terms appear, use exactly the required "
            "translation (source -> required translation):");
        for (const Settings::TermPair &term : terms)
            t += QStringLiteral("\n- %1 -> %2").arg(term.first, term.second);
        blocks += t;
    }

    return blocks.isEmpty() ? QString() : QStringLiteral("\n\n") + blocks.join(QLatin1Char('\n'));
}

} // namespace

Translator::Translator(QObject *parent)
    : QObject(parent)
{
}

void Translator::translate(const QString &text, const QString &fromLang, const QString &toLang,
                           bool isOcr, LlmAction action)
{
    if (m_busy || text.trimmed().isEmpty())
        return;

    // 保存待翻译参数：若端点拒绝 thinking 禁用字段，降级重发时复用
    m_pendingText = text;
    m_pendingFrom = fromLang;
    m_pendingTo = toLang;
    m_pendingIsOcr = isOcr;
    m_pendingAction = action;
    m_tryDisableThinking = true;
    translateByApi(text, fromLang, toLang, isOcr, true, action);
}

void Translator::translateByApi(const QString &text, const QString &fromLang, const QString &toLang,
                                bool isOcr, bool disableThinking, LlmAction action)
{
    const Settings &s = Settings::instance();
    const QString baseUrl = s.apiBaseUrl();
    const QString apiKey  = s.apiKey();
    const QString model   = s.apiModel();
    const ApiFormat format = apiFormatFromSetting(s.apiFormat());

    if (baseUrl.isEmpty() || apiKey.isEmpty()) {
        m_busy = false;
        emit translateFinished(false, QString(), "未配置 API（base_url / api_key）");
        return;
    }

    const QString base = normalizeApiBaseUrl(baseUrl);
    QNetworkRequest req(QUrl(base + apiEndpointPath(format, base)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyApiAuthHeaders(req, format, apiKey);
    // 传输超时：服务端挂起时原先没有超时，m_busy 永真、后续请求被静默丢弃
    // 且无任何报错——用户视角是"翻译永远转圈"。超时后 finished 触发，
    // onReplyFinished 按 OperationCanceledError 报错复位
    req.setTransferTimeout(15000);

    // 动作提示词（纯函数）；领域与术语表仅对翻译动作注入——术语映射指令
    // （"必须使用指定译名"）对解释/总结/改写只会造成干扰
    QString systemPrompt = translateActionPrompt(action, fromLang, toLang, isOcr);
    if (action == LlmAction::Translate)
        systemPrompt += buildDomainAndTermsPrompt();

    // 请求体构造收敛为纯函数 buildTranslateRequestBody（Translator.h）：
    // 双协议只此一处实现，ApiTester 与单元测试共用
    const QByteArray payload =
        buildTranslateRequestBody(format, model, systemPrompt, text, disableThinking);

    m_busy = true;
    // reply 归 m_nam 父子所有，收尾经 sender() 取回并 deleteLater；
    // 无需成员保存（原 m_reply 只写不读，T-062 删除）
    QNetworkReply *reply = m_nam.post(req, payload);
    connect(reply, &QNetworkReply::finished, this, &Translator::onReplyFinished);
}

// 请求收尾：端点拒绝 thinking 禁用字段（400/422 且报文为空或疑似"字段不识别"）时
// 去字段降级重发一次；其余错误把 Qt 错误串/HTTP 状态翻成人话；成功按协议
// 格式提取译文并兜底清洗 <think> 思维链。reply 生命周期归 deleteLater。
void Translator::onReplyFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    reply->deleteLater();

    const QByteArray raw = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 服务端错误文案：下方降级判定与错误文案两处共用一次解析（JSON 解析幂等，
    // 提前提取无副作用；成功路径不解析）
    QString serverMsg;
    if (reply->error() != QNetworkReply::NoError) {
        const QJsonDocument errDoc = QJsonDocument::fromJson(raw);
        if (errDoc.isObject())
            serverMsg = errDoc.object().value("error").toObject().value("message").toString();
    }

    // 端点不认识 thinking 禁用字段（严格校验的服务返回 400/422，两种格式均可能）：
    // 去掉该字段降级重发一次，仅损失"关思考"优化，不影响翻译可用性。
    // 只对"字段不被识别"类拒绝重发——模型名错误等真 400 重发也不会成功，白发
    if (reply->error() != QNetworkReply::NoError && m_tryDisableThinking
        && (httpStatus == 400 || httpStatus == 422)) {
        static const QRegularExpression fieldRejection(
            QStringLiteral("thinking|unknown (field|parameter)|unrecognized|unexpected|unsupported"),
            QRegularExpression::CaseInsensitiveOption);
        if (serverMsg.isEmpty() || fieldRejection.match(serverMsg).hasMatch()) {
            m_tryDisableThinking = false;
            translateByApi(m_pendingText, m_pendingFrom, m_pendingTo, m_pendingIsOcr, false,
                           m_pendingAction);
            return; // 保持 m_busy，等待重发请求完成
        }
    }

    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        // 把晦涩的 Qt 错误串翻译成可读原因：优先透出服务端 JSON 的 error.message
        //（如"Key 无效""模型不存在"）。注意 Qt 将 HTTP 401 一律映射为
        // "Host requires authentication"，必须转成人话
        QString msg;
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            msg = QStringLiteral("连接超时（15 秒无响应），请检查网络或代理设置");
        } else if (httpStatus == 401 || httpStatus == 403) {
            msg = QStringLiteral("API 认证失败（%1）：API Key 无效或未授权，"
                                 "请在设置中重新填写 Key 并用「测试连接」验证").arg(httpStatus);
        } else if (httpStatus == 400 || httpStatus == 422) {
            msg = QStringLiteral("请求被拒绝（%1）：%2")
                      .arg(httpStatus)
                      .arg(serverMsg.isEmpty() ? reply->errorString() : serverMsg);
        } else {
            msg = serverMsg.isEmpty()
                      ? reply->errorString()
                      : QStringLiteral("服务端返回 %1：%2").arg(httpStatus).arg(serverMsg);
        }
        emit translateFinished(false, QString(), msg);
        return;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) {
        emit translateFinished(false, QString(), "响应解析失败: " + perr.errorString());
        return;
    }

    // 按格式提取译文：
    //   OpenAI    → choices[0].message.content
    //   Anthropic → content[] 块数组，仅拼接 type=="text" 的块
    //   （thinking 等其他块类型忽略）
    QString result;
    // 按当前设置判定协议：请求在途期间用户改格式理论上会错位（边缘，接受；
    // 缓存请求时格式属行为增强，T-062 登记不改）
    if (apiFormatFromSetting(Settings::instance().apiFormat()) == ApiFormat::Anthropic) {
        const QJsonArray blocks = doc.object().value("content").toArray();
        for (const QJsonValue &block : blocks) {
            const QJsonObject obj = block.toObject();
            if (obj.value("type").toString() == QLatin1String("text"))
                result += obj.value("text").toString();
        }
    } else {
        result = doc.object()
                     .value("choices").toArray()
                     .at(0).toObject()
                     .value("message").toObject()
                     .value("content").toString();
    }

    // 兜底清洗：部分模型（如 Qwen3 经第三方中转）会把思维链以 <think>…</think>
    // 混在 content 里输出，取闭合标签之后的正文；只有未闭合思维链则视为空结果
    const int thinkEnd = result.lastIndexOf(QStringLiteral("</think>"));
    if (thinkEnd != -1)
        result = result.mid(thinkEnd + 8); // +8 = "</think>".length()
    else if (result.contains(QStringLiteral("<think>")))
        result.clear();

    result = result.trimmed();
    if (result.isEmpty())
        emit translateFinished(false, QString(), "API 返回空结果");
    else
        emit translateFinished(true, result, QString());
}

} // namespace poptools
