#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

#include "Translator.h"

class QNetworkReply;

namespace snipaste {

// ============================================================
// API 连接测试（设置对话框「测试连接」按钮用）
//
// 用对话框表单里当前填写的接口格式 / Base URL / API Key / Model
// （而非已保存的 Settings）发一次最小的翻译请求，把结果翻译成人话：
//   * 200        → 连接成功
//   * 401        → Key 无效或未授权
//   * 400/422    → 服务端拒绝（透出 error.message，如模型名不存在）
//   * 超时/断网  → 对应提示
// ============================================================
class ApiTester : public QObject
{
    Q_OBJECT
public:
    explicit ApiTester(QObject *parent = nullptr);

    // 发起测试（异步），完成或超时后发 finished；测试期间重复调用被忽略
    void test(const QString &baseUrl, const QString &apiKey, const QString &model,
              ApiFormat format);

    bool busy() const { return m_busy; }

signals:
    void finished(bool ok, const QString &message);

private:
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
};

} // namespace snipaste
