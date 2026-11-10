#pragma once

#include "../IFeatureModule.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>

class QAction;

namespace poptools {

class ActivityStore;
class ChatSession;
class DailyRecordJob;
class GoodyWindow;
class LlmClient;
class McpStdioClient;
class MemoryStore;
class PermissionManager;
class Scheduler;
class ToolRegistry;
class WorkingMemory;

// ============================================================
// Goody 管家功能模块：内置大模型助手（DeepSeek，OpenAI 兼容协议）。
//
// 职责：
//   * 聊天窗（GoodyWindow，Ctrl+Alt+G 呼出 / 托盘入口）+ 多轮会话持久化；
//   * 工具调用回合循环：模型发起 tool_calls → 权限决策（三模式，确认
//     卡片内联聊天流）→ ToolRegistry 执行 → 结果回传 → 继续生成；
//   * 人格系统提示词（专业温暖的小管家，只做电脑整理/电脑问题/记录）；
//   * 审计日志（goody/audit.jsonl，复用 ActivityStore worker 写盘）。
//
// 每日工作记录、记忆系统、MCP 在后续阶段接入本模块（P4/P5/P6）。
// ============================================================
class GoodyModule : public QObject, public IFeatureModule
{
    Q_OBJECT
public:
    explicit GoodyModule(QObject *parent = nullptr);
    QString id() const override { return QStringLiteral("goody"); }
    void init(AppServices &services) override;
    void shutdown() override;

private slots:
    void onSettingsChanged();

private:
    void ensureWindow();
    void openChat();
    void registerBuiltinTools();
    void setupMcp(); // mcp.json 服务器拉起 + 工具桥接（P6）
    void applySummarySchedule(); // 总结调度接线（init 与设置变更共用）
    void sendMessage(const QString &text);
    void runTurn();
    void onTurnFinished(bool ok, const QJsonObject &assistantMessage, const QString &error);
    void processNextToolCall();
    void executeToolCall(const QString &toolCallId, const QString &name,
                         const QJsonObject &args);
    void toolCallDone(const QString &toolCallId, const QJsonObject &result);
    QString buildSystemPrompt() const;
    QString summarizeArgs(const QJsonObject &args) const;
    QString summarizeResult(const QJsonObject &result) const;

    AppServices *m_services = nullptr;
    QAction *m_trayAction = nullptr;

    ToolRegistry *m_registry = nullptr;   // 组合根创建，各模块共用
    ActivityStore *m_auditStore = nullptr; // goody 目录审计流（worker 写盘）
    PermissionManager *m_perm = nullptr;
    LlmClient *m_client = nullptr;
    ChatSession *m_session = nullptr;
    GoodyWindow *m_window = nullptr;
    DailyRecordJob *m_recordJob = nullptr; // 本日工作记录任务（P4）
    Scheduler *m_scheduler = nullptr;      // 每日到点调度（P4）
    MemoryStore *m_memory = nullptr;       // 长期记忆（P5）
    WorkingMemory *m_work = nullptr;       // 瞬时工作记忆（P5）
    QList<McpStdioClient *> m_mcpClients;  // MCP 服务器连接（P6）

    QJsonArray m_pendingToolCalls; // 当前助手消息中待执行的工具调用
    int m_toolRounds = 0;          // 单次用户消息的工具回合计数（防失控循环）
    bool m_toolPhase = false;      // 工具执行期（tool_calls 回填中）：sendMessage 守卫
    qint64 m_toolEpoch = 0;        // 会话清空收束代数（清空时 +1）：在途工具/
                                   // 确认回调按代数失配丢弃，防孤儿 tool 入会话
    qint64 m_activeTurnEpoch = -1; // 当前在途 LLM 回合发起时的代数快照：
                                   // onTurnFinished 与 m_toolEpoch 失配即迟到
                                   // 回包（会话已清空），整体丢弃不入新会话（C-N2）
};

} // namespace poptools
