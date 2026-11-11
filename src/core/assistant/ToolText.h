#pragma once

#include <QJsonObject>
#include <QString>

namespace poptools {

// ============================================================
// 工具调用行的纯格式化（R6 测试补齐轮抽取自 GoodyModule 成员函数，
// 行为逐字节不变）：每次工具调用在聊天窗渲染「⚙ 调用工具 …」与
// 「✓ 完成/✗ 失败 …」共用的两段摘要逻辑，无状态、无副作用，抽出
// 便于单测与复用。
// ============================================================

// 参数摘要：紧凑 JSON 全文，超 200 字符截断加"…"（聊天窗单行展示体量护栏）
QString summarizeToolArgs(const QJsonObject &args);

// 结果摘要：ok:false 取 error 文案（空 error → "执行失败"）；ok:true 把
// 其余键拼成 "key=value " 列表——字符串原样，非字符串值剥壳 JSON，超
// 160 字符截断加"…"
QString summarizeToolResult(const QJsonObject &result);

} // namespace poptools
