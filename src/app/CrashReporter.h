#pragma once

namespace poptools {

// 崩溃自诊（T-051）：未处理异常 / qFatal / CRT 中止 → exe 旁 crash/ 目录写
// MiniDump + 符号化调用栈日志（函数名 + 源文件行号，Release 需 PDB，见根
// CMakeLists POPTOOLS_RELEASE_PDB）。起因：带模式 DComp 上传崩溃在用户
// 真机必现、调试器下不复现，需要无调试器干扰的现场证据；此后一切崩溃
// 自动留档，替代被 WER 上传后清空的空壳报告。
class CrashReporter
{
public:
    // 在 main 最先调用（早于 QApplication，捕获早期崩溃）；进程内幂等
    static void install();
};

} // namespace poptools
