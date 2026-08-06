# DEBUG 记录

## 1. Qt 6.12.0 无法用 aqt 3.3.0 安装
- **现象**：`aqt install-qt windows desktop 6.12.0` 报错 `Failed to locate XML data for Qt version '6.12.0'`，实际 URL 指向 `qt6_6120/qt6_6120/Updates.xml`（404）
- **根因**：Qt 6.12 起官方仓库改为新目录布局：`qt6_6120/` 根目录 + `qt6_6120_msvc2022_64/` 架构子目录（目录名含完整补丁号）。aqt 3.3.0 仍按旧布局 `qt6_612/` 拼接 URL
- **解决**：改用 Qt 6.8.3 LTS（旧布局，aqt 3.3.0 兼容，LTS 稳定，适合个人项目）
- **教训**：aqt 与最新 Qt 版本存在兼容滞后；使用 LTS 版本更稳妥

## 2. 命令行直接运行 cl.exe 报错
- **现象**：PowerShell 中 `cl` 无法识别
- **根因**：MSVC 工具链不在 PATH，需通过 `vcvarsall.bat x64` 或 CMake 自动配置环境变量（INCLUDE/LIB/PATH）
- **解决**：本项目全程用 CMake 配置 MSVC，无需手动 vcvars
