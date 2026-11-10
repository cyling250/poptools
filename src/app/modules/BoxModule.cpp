#include "BoxModule.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QStandardPaths>
#include <QTimer>

#include "../AppServices.h"
#include "core/assistant/ToolRegistry.h"
#include "core/box/BoxStore.h"
#include "core/settings/Settings.h"
#include "ui/box/BoxWindow.h"

namespace poptools {

namespace {
// 盒名清洗：盒名即桌面真实目录名。含路径分隔符 / Windows 目录非法
// 字符 / 点段（".."\\"."）的名称一律拒绝——防 LLM 或手输的 "..\\..\\x"
// 经 QDir 归一化后在桌面之外创建目录树。返回空串表示非法
QString sanitizeBoxName(const QString &raw)
{
    const QString name = raw.trimmed();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QString();
    if (name.endsWith(QLatin1Char('.'))) // Windows 目录名不允许结尾点
        return QString();
    for (const QChar &c : name) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('/') || c == QLatin1Char(':')
            || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
            || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|'))
            return QString();
    }
    return name;
}
} // namespace

BoxModule::BoxModule(QObject *parent)
    : QObject(parent)
{
}

void BoxModule::init(AppServices &services)
{
    m_services = &services;
    m_store = services.boxStore;
    Q_ASSERT(m_store && "组合根必须先创建共享 BoxStore 再装配模块");
    m_store->load();

    // 设置页保存后：按「显示桌面收纳盒」开关显隐
    connect(&services, &AppServices::settingsChanged, this, &BoxModule::onSettingsChanged);

    // 托盘入口：新建收纳盒（模块关闭时点击自动启用）
    services.addTrayAction(QStringLiteral("新建收纳盒…"), [this] { createBox(); });

    // 仓库变化（含其他入口的增删）→ 窗口集合同步
    connect(m_store, &BoxStore::changed, this, &BoxModule::refreshBoxes, Qt::QueuedConnection);

    // 设置开启时随应用启动显示（窗口在事件循环就绪后创建）
    if (Settings::instance().organizerEnabled())
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [this] { applyVisibility(); });

    // Goody 工具贡献：模型可在桌面新建收纳盒（创建真实文件夹 → 走写类权限）
    if (services.toolRegistry) {
        ToolDef d;
        d.name = QStringLiteral("app.box_create");
        d.readOnly = false;
        d.description = QStringLiteral(
            "在桌面新建一个 PopTools 收纳盒（同名真实文件夹）。"
            "会实际创建文件夹，先向用户确认名称。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"name", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("收纳盒名称")}}}}},
            {"required", QJsonArray{QStringLiteral("name")}},
        };
        d.fn = [this](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            const QString name = sanitizeBoxName(args.value(QStringLiteral("name")).toString());
            if (name.isEmpty()) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("名称为空或含非法字符（路径分隔符/"
                                                          "点段/Windows 保留字符），已拒绝")}});
                return;
            }
            const QString dir = QDir(QStandardPaths::writableLocation(
                                         QStandardPaths::DesktopLocation))
                                    .filePath(name);
            if (QFileInfo::exists(dir)) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("桌面已存在同名目录：%1").arg(name)}});
                return;
            }
            int x = 60, y = 200;
            const QList<DesktopBox> boxes = m_store->boxes();
            if (!boxes.isEmpty()) {
                x = boxes.last().x + 260;
                y = boxes.last().y;
            }
            if (!Settings::instance().organizerEnabled())
                Settings::instance().setOrganizerEnabled(true);
            m_store->addBox(name, dir, x, y);
            QTimer::singleShot(0, QCoreApplication::instance(),
                               [this] { applyVisibility(); });
            done(QJsonObject{{"ok", true},
                             {"dir", QStandardPaths::writableLocation(
                                         QStandardPaths::DesktopLocation)
                                         + QStringLiteral("/") + name}});
        };
        services.toolRegistry->registerTool(d);
    }
}

void BoxModule::shutdown()
{
    // 无系统级资源；数据随每次修改即时落盘，窗口随 QApplication 退出清理
    for (BoxWindow *w : m_windows)
        w->hide();
}

// 按设置开关 + 仓库内容同步窗口集合（增盒建窗、删盒关窗）
void BoxModule::applyVisibility()
{
    const bool enabled = Settings::instance().organizerEnabled();
    if (!enabled) {
        for (BoxWindow *w : m_windows)
            w->hide();
        return;
    }
    refreshBoxes();
    for (BoxWindow *w : m_windows) {
        // 不 raise()：贴桌窗口必须待在一切普通窗口之下，
        // 显示后由贴桌守护定时器归位
        w->show();
    }
}

// 仓库内容 → 窗口集合对齐：删盒关窗、新盒建窗
void BoxModule::refreshBoxes()
{
    if (!Settings::instance().organizerEnabled())
        return; // 关闭态不建窗（applyVisibility 统一隐藏）

    QStringList ids;
    for (const DesktopBox &box : m_store->boxes())
        ids << box.id;

    // 回收已删除盒子的窗口
    for (int i = m_windows.size() - 1; i >= 0; --i) {
        BoxWindow *w = m_windows.at(i);
        if (!ids.contains(w->boxId())) {
            w->close();
            w->deleteLater();
            m_windows.removeAt(i);
        }
    }

    // 新盒子建窗（已存在的盒子窗口自行响应 changed 刷新）
    for (const DesktopBox &box : m_store->boxes()) {
        bool exists = false;
        for (BoxWindow *w : m_windows)
            if (w->boxId() == box.id)
                exists = true;
        if (exists)
            continue;
        BoxWindow *w = ensureWindow(box.id);
        w->move(box.x, box.y);
        w->show();
    }
}

BoxWindow *BoxModule::ensureWindow(const QString &boxId)
{
    for (BoxWindow *w : m_windows)
        if (w->boxId() == boxId)
            return w;
    auto *w = new BoxWindow(m_store, boxId); // 随 QApplication 退出清理
    connect(w, &BoxWindow::storageError, this, [this](const QString &msg) {
        if (m_services && m_services->notify)
            m_services->notify(QStringLiteral("桌面收纳盒"), msg);
    });
    m_windows << w;
    return w;
}

// 托盘「新建收纳盒…」：命名 → 目录默认建在桌面（已有同名文件夹
// 则直接收纳它）。模块关闭时自动启用
void BoxModule::createBox()
{
    const QString name = sanitizeBoxName(QInputDialog::getText(
        nullptr, QStringLiteral("新建收纳盒"), QStringLiteral("名称：")));
    if (name.isEmpty())
        return;
    const QString dir = QDir(QStandardPaths::writableLocation(
                                 QStandardPaths::DesktopLocation))
                            .filePath(name);

    // 新盒位置：沿已有盒子右侧依次排开，首盒放屏幕右侧中部
    int x = 60, y = 200;
    const QList<DesktopBox> boxes = m_store->boxes();
    if (!boxes.isEmpty()) {
        const DesktopBox &last = boxes.last();
        x = last.x + 260;
        y = last.y;
    }

    if (!Settings::instance().organizerEnabled()) {
        Settings::instance().setOrganizerEnabled(true);
        applyVisibility();
    }
    m_store->addBox(name, dir, x, y);
}

void BoxModule::onSettingsChanged()
{
    applyVisibility();
}

} // namespace poptools
