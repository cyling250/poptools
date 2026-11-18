# T-066 细档：TSF 被动观察 TIP 技术调研报告（2026-11-18）

> 调研执行：general-purpose 子代理（微软官方文档＋安全研究/开源先例＋GitHub 源码在线浏览）＋本机 Win11 build 26300 实证（按 AGENTS.md 下载与逆向纪律：weasel 官方安装包 安装→检查→卸载→恢复基线，注册表逐键比对一致，文件与下载物全部清除；探针产物留存 `temp_scripts/weasel_probe/`）。调研期间 ctfmon 重启过一次（卸载删键纪律的一部分）。
> 实证工具：Rime weasel 0.17.4（开源第三方中文 TIP，全程未签名）。

---

## 1.【加载模型】未被选中的第三方 TIP 会不会被加载进应用进程

**结论**
- 官方文档模型：**TSF 在应用启动时只把"用户当前选中的输入法"的 DLL 加载进应用进程**。确证。
- 本机实测（build 26300）：**注册＋启用（Enable=1）但未被选中 → 不被加载**。全进程扫描（全新启动的 notepad、ctfmon、explorer、TextInputHost、SearchHost、sihost）均无 weasel DLL 模块。确证。
- Praetorian 红队文章"注册后 Windows 自动把 text service 载入利用 TSF 的应用"的说法：在本机 26300 上**不成立**（至少对"仅注册/启用、未选中"场景）。存疑。
- `ITfTextInputProcessor::Activate` 由 TSF 在 `CoCreateInstance` 创建实例**之后**调用（文档 Remarks 原文），即：激活=CoCreateInstance＋Activate。未被选中→无 Activate→无法 advise 任何 sink→拿不到任何编辑事件。确证。
- **同一语言同一时刻只有一个键盘类别 TIP 活动**——微软官方答复原句："only one keyboard-category TIP is active per language, so a keyboard TIP cannot run alongside Microsoft IME"。确证。
- **`GUID_TFCAT_TIP_OTHER` 并不存在**：Windows SDK 10.0.26100 的 `msctf.h` 只声明 `GUID_TFCAT_TIP_KEYBOARD / _SPEECH / _HANDWRITING` 三个 TIP 类别（第 844–846 行），官方 Predefined Category Values 页同样只列这三个。确证（此条修正了调研提问里的前提）。
- 非键盘类别（SPEECH/HANDWRITING）TIP 能否与活动键盘 IME 并存激活：`ActivateProfile`/`ActivateLanguageProfile` 的参数表都**没有类别参数**（激活模型以键盘类别为中心），公开资料无系统自动激活非键盘 TIP 的证据（语音 TIP 由语音功能开启驱动）。存疑。

**关键细节**
- DLL 路径来自 `HKCR\CLSID\{clsid}\InprocServer32`（本机实测 weasel 指向 `C:\WINDOWS\system32\weasel.dll`，ThreadingModel=Apartment）。
- 用户输入法启用列表实测（HKCU\Software\Microsoft\CTF\SortOrder\AssemblyItem\0x00000804\{34745C63-B2F0-4784-8B67-5E12C8701A31}）：第 0 位微软拼音、第 1 位 weasel——weasel 处于"已启用、未选中"状态，这就是实测不加载的条件本身。

**来源**
- [IME requirements（官方："When an app starts, TSF loads the IME DLL for the IME that's currently selected by the user"）](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements)
- [Third-party input method editors（官方同句）](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/third-party-input-method-editors)
- [微软 Q&A：被动 observer TIP 实战（含"one keyboard-category TIP per language"）](https://learn.microsoft.com/en-ca/answers/questions/6007678/tsf-itftexteditsink-onendedit-observes-direct-asci)
- [Praetorian: Leveraging TSF for Red Team Operations](https://www.praetorian.com/blog/leveraging-microsoft-text-services-framework-tsf-for-red-team-operations)
- [ITfTextInputProcessor::Activate](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftextinputprocessor-activate)
- [Predefined Category Values](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-category-values)
- 本机实测（2026-11-18，build 26300）

---

## 2.【签名门槛】未签名/自签 TIP 能否注册并被加载

**结论**
- 官方要求：Windows 8 起"**Third-party IMEs must be digitally signed**"，且"All third-party IMEs must be digitally signed in order to be installed onto the Windows 8 system as an IME"；但同文档措辞是"Only IMEs that have this digital signature can be installed onto the system **without having a critical warning message** appear to the user"——语义偏向警告而非内核式硬阻断。确证（要求文本）/较可信（非硬阻断的解读）。
- **实证：完全未签名的第三方中文 IME 在 Win11 26300 上安装、注册、枚举、运行全部正常**。weasel 0.17.4.0 安装包及安装目录全部 11 个 DLL/EXE（含 TSF 服务 DLL `weasel.dll`/`weaselx64.dll`）`Get-AuthenticodeSignature` 全部 `NotSigned`；官方 CI（ci.yml）与 xmake.lua/build 脚本零签名步骤。确证。
- 边界条件：Secure Mode（UAC 安全桌面）需注册 `GUID_TFCAT_TIPCAP_SECUREMODE`；Windows Store/immersive 应用要求 IMMERSIVESUPPORT 类别；Smart App Control/WDAC 开启时会拦未签名 DLL；SmartScreen 对未签名下载有警告。较可信。
- 本地受信任自建证书自签：按 Authenticode 逻辑可行（比 weasel 的"完全不签"更严），但无官方逐案保证。较可信。

**关键细节**
- 开源第三方输入法的实际解法：**weasel 干脆不签名**，靠 HKLM 注册进系统，是"签名要求在桌面环境不构成硬门槛"的最强活证。
- weasel issue #1962 显示其未签名 DLL 实际被宿主进程（Opera，Win11 25H2）加载运行。

**来源**
- [Third-party input method editors（签名要求原文）](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/third-party-input-method-editors)
- [Text Service Registration（"should also provide digital signatures"）](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)
- [weasel CI 工作流（无签名步骤）](https://github.com/rime/weasel/blob/master/.github/workflows/ci.yml)
- [weasel 0.17.4 release](https://github.com/rime/weasel/releases/tag/0.17.4)
- [weasel issue #1962（未签名 DLL 在 Win11 25H2 宿主进程内运行）](https://github.com/rime/weasel/issues/1962)
- 本机实测签名扫描（2026-11-18）

---

## 3.【取文 API】OnEndEdit / ITfEditRecord 的准确用法

**结论**
- `OnEndEdit(pic, ecReadOnly, pEditRecord)`：ecReadOnly 只能用于查看；要改动必须自己另发异步 `RequestEditSession`；可用 `InWriteSession` 判断本次编辑是否自己发起。确证。
- **`GetUpdatedTextRanges` 这个方法并不存在**：本机 `msctf.h`（10.0.26100）中 `ITfEditRecord` 只有 `GetSelectionStatus`（行 4673）和 `GetTextAndPropertyUpdates`（行 4676）两个方法；Learn 上无该方法页（404），检索亦确认无此 API。确证。正确用法：`GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0)`——返回覆盖本次编辑变更文本的 `IEnumTfRanges`。
- **删除的表示**：官方文档未写明。机制推断：返回范围覆盖的是编辑会话结束后的当前文档状态，纯删除表现为塌缩（零长）范围，**被删内容本身读不回来，必须调用方自留影子副本做差分**。存疑（推断合理但无官方文本）。
- **读全文**：可以。用 ecReadOnly 构造覆盖全文档的 `ITfRange`（`ITfContext::GetStart/GetEnd`），`ITfRange::GetText(ec, ...)` 读取；cookie 非只读锁时返回 `TF_E_NOLOCK`。确证。
- **决定性宿主限制**（微软官方答复）：经典 EDIT 控件无 text store（`TS_SS_TRANSITORY`，只容纳 composition），直接键入对 TSF 完全不可见；RichEdit 默认关闭 TSF（`EM_SETEDITSTYLE(SES_USECTF)` 打开后即可观察，实测与打包版 Notepad 行为一致）；无 text store 宿主里唯一的 TSF 数据是按键（`ITfKeyEventSink`），而它要求你的 TIP 是活动键盘 TIP。确证。

**关键细节**
- 调用链：`OnEndEdit` → `pEditRecord->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0, &pEnum)` → 枚举 `ITfRange` → `GetText(ecReadOnly)`。
- IME 中文上屏＝活动 IME 发起的 commit 编辑会话，OnEndEdit 对该 context 上所有已 advise 的 sink 广播——TSF-aware 宿主里 observer 可见（较可信，Q&A 实测直接 ASCII 输入亦能收到）。

**来源**
- [ITfTextEditSink::OnEndEdit](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftexteditsink-onendedit)
- [ITfEditRecord](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfeditrecord)
- [ITfEditRecord::GetTextAndPropertyUpdates](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfeditrecord-gettextandpropertyupdates)
- [ITfRange::GetText](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-gettext)
- [微软 Q&A 实战（TRANSITORY 宿主/SES_USECTF/UIA 替代）](https://learn.microsoft.com/en-ca/answers/questions/6007678/tsf-itftexteditsink-onendedit-observes-direct-asci)
- 本机 `D:\Windows Kits\10\Include\10.0.26100.0\um\msctf.h`（行 4673–4746，接口方法实证）

---

## 4.【注册与安装】RegisterProfile、按用户注册、卸载流程

**结论**
- **按用户（HKCU、免管理员）注册 TIP：不可行**（较可信，接近确证）。TIP 的 COM CLSID 与 CTF\TIP 数据必须在机器级（HKLM/HKCR）；官方 API 从未提供 per-user TIP 注册；Praetorian 实测"HKCU entries do not result in functional plugins"；本机实测 weasel 全部写 HKLM/HKCR（安装需 UAC），本机 HKCU\Software\Microsoft\CTF\TIP 只有系统原有条目。HKCU 只承载"启用状态/排序"（SortOrder）。
- 注册步骤清单（官方文档＋weasel 源码＋本机实测注册表布局三方一致）：
  1. `DllRegisterServer`：写 `HKCR\CLSID\{clsid}`（默认值=名称）＋`InprocServer32`（默认值=DLL 路径，`ThreadingModel=Apartment`）。
  2. `ITfCategoryMgr::RegisterCategory(clsid, GUID_TFCAT_TIP_KEYBOARD, clsid)`＋capability 类别（SECUREMODE/UIELEMENTENABLED/COMLESS/IMMERSIVESUPPORT/SYSTRAYSUPPORT 等）——落盘为 `HKLM\SOFTWARE\Microsoft\CTF\TIP\{clsid}\Category\Category\{catGuid}\{clsid}` 与 `Category\Item\{clsid}\{catGuid}` 双向缓存（本机实测键树）。
  3. `ITfInputProcessorProfileMgr::RegisterProfile(clsid, langid, guidProfile, pchDesc, cchDesc, pchIconFile, cchIcon, uIconIndex, hklsubstitute=NULL, dwPreferredLayout=0, bEnabledByDefault, dwFlags)`——落盘 `HKLM\...\TIP\{clsid}\LanguageProfile\{langid}\{guidProfile}`，值：Description/IconFile/IconIndex/**Enable(=bEnabledByDefault)**（本机实测：weasel 对 5 个 LANGID 各注册一个 profile，仅 0x0804 Enable=1）。flags 可选 TF_RP_HIDDENINSETTINGUI/LOCALPROCESS/LOCALTHREAD。
  4. 启用进用户列表：Enable=1 会同步写 `HKCU\...\SortOrder\AssemblyItem\{langid}\{34745C63-B2F0-4784-8B67-5E12C8701A31}\{序号}`（CLSID/KeyboardLayout/Profile 三值，本机实测）；等价 API 还有 `InstallLayoutOrTip("0x0804:{clsid}{guidProfile}",...)`（官方 psz 格式）与 `EnableLanguageProfile(clsid, langid, guidProfile, TRUE)`（官方 Remarks："for the current user"）。
  5. 启用≠激活；激活靠用户切换或 `ActivateProfile`（TF_IPPMF_FORPROCESS/FORSESSION/ENABLEPROFILE…）。
- **注销/卸载**：`ITfInputProcessorProfiles::Unregister`（顺带清 profile 与类别）＋删 HKCR CLSID 键＋**手动删 HKLM CTF\TIP\{clsid} 键**。两个实测新发现：①weasel 卸载器（NSIS /S）本身不清理任何 TSF 注册表键（与源码注释"On Windows 8, we need to manually delete the registry key for our TIP"呼应）；②**删键后 ctfmon 会用内存缓存把键回写复活——必须先结束 ctfmon 再删键**，重启后即不再复活（本机实测）。
- **weasel 先例源码**：[WeaselTSF/Register.cpp](https://github.com/rime/weasel/blob/master/WeaselTSF/Register.cpp)——`RegisterProfiles/UnregisterProfiles/RegisterCategories/UnregisterCategories/RegisterServer/UnregisterServer`；`ITfInputProcessorProfileMgr::RegisterProfile`（5×LANGID：Hans/Hant/HK/Macau/SG）＋`ITfCategoryMgr::RegisterCategory`＋手写 HKCR CLSID 键；ARM64X 重定向处理。配套：`Globals.cpp`（CLSID/GUID）、`dllmain.cpp`、`Server.cpp`、`output/install.nsi`。

**来源**
- [ITfInputProcessorProfileMgr::RegisterProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-registerprofile)
- [EnableLanguageProfile（"for the current user"）](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-enablelanguageprofile)
- [InstallLayoutOrTip（psz 格式）](https://learn.microsoft.com/en-us/windows/win32/tsf/installlayoutortip)
- [Text Service Registration](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)
- [ActivateProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile) ｜ [ActivateLanguageProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-activatelanguageprofile)
- [weasel Register.cpp](https://github.com/rime/weasel/blob/master/WeaselTSF/Register.cpp)
- Praetorian（HKCU 不生效）＋本机实测键树（2026-11-18）

---

## 5.【先例】TSF 观察类工具的加载模型

**结论**
- Praetorian（2025-02，安全研究）：TIP 作为持久化载体，HKLM 注册＋语言 profile；声称注册即自动加载。其"自动加载"表述与本次本机实测矛盾（未选中不加载）；作为先例证明的是 TSF TIP 可承载输入面持久化，检测面是 HKLM CTF\TIP 键与进程内陌生 DLL。较可信（方法论可靠，加载细节表述不准）。
- 微软 Q&A 观察型 TIP（最贴近本项目场景）：观察者以**另一语言（en-US）的活动键盘 TIP**身份与 zh-CN 微软拼音并存实测成功；TRANSITORY 宿主盲区；官方点名 UI Automation 文本变化事件为无 text store 宿主的替代路径。较可信。
- weasel/搜狗等输入法：**被选中才激活**的键盘 TIP 模型（默认模型）；issues 证明其 DLL 实际进宿主进程（#1962/#1952）。确证。
- Punto Switcher：**不走 TSF**——全局键盘钩子（WH_KEYBOARD/LL）＋`ActivateKeyboardLayout`/`WM_INPUTLANGCHANGEREQUEST` 切换。较可信。
- 学术面：TSF 键盘记录专题论文罕见；仅有 TestIME（PMC 2019）用 TSF 做 IME 评估。较可信。

**来源**
- [Praetorian TSF red team](https://www.praetorian.com/blog/leveraging-microsoft-text-services-framework-tsf-for-red-team-operations)
- [微软 Q&A observer TIP](https://learn.microsoft.com/en-ca/answers/questions/6007678/tsf-itftexteditsink-onendedit-observes-direct-asci)
- [weasel #1962](https://github.com/rime/weasel/issues/1962) ｜ [#1952](https://github.com/rime/weasel/issues/1952)
- [Punto Switcher 官方页](https://yandex.ru/soft/punto/win)
- [TestIME (PMC6894108)](https://pmc.ncbi.nlm.nih.gov/articles/PMC6894108)

---

## 6.【Win11 24H2+ 变化】input experience 重写对第三方 TIP 的影响

**结论**
- **第三方 TIP 在 Win11 24H2/25H2 上仍然正常工作**：rime.im 官方声明 weasel 支持 Windows 8.1/10/11；本机 26300 实测注册、枚举、启用、服务进程全部正常；weasel issues 显示 25H2 上 DLL 照常进宿主进程。确证。
- 已知回归（2024–2026）：2025-07 KB5062553 致繁中仓颉等 IME 输入异常（官方 release health 跟踪、后续累积更新修复）；24H2 功能更新会把用户已装第三方输入法从默认/启用列表重置（社区报告）；weasel 个体宿主 bug：#1335 任务栏点击冻结（2024）、#1586 Edge 无法输入（0.17.0）、#1962 Opera/25H2 崩溃（0.17.4）。形态是"个别宿主 bug＋月度更新回归"，不是平台放弃第三方 TIP。确证（官方页面）/较可信（社区面）。
- input experience（TextInputHost）重写未见官方"改变第三方 TIP 加载模型"的声明；本机实测加载模型与 Win8 以来的官方文档完全一致（选中才加载）。较可信：无结构性破坏，但需按月跟踪 release health。

**来源**
- [Windows 11 24H2 known issues（2026-11 已无 IME 开放项）](https://learn.microsoft.com/en-us/windows/release-health/status-windows-11-24h2)
- [Resolved issues in Windows 11 24H2](https://support.microsoft.com/en-us/windows/resolved-issues-in-windows-11-version-24h2)
- [rime.im 下载页（支持 Win 8.1/10/11）](https://rime.im/download/)
- [weasel #1962](https://github.com/rime/weasel/issues/1962) ｜ [#1974](https://github.com/rime/weasel/issues/1974) ｜ [#1586](https://github.com/rime/weasel/issues/1586) ｜ [#1335](https://github.com/rime/weasel/issues/1335) ｜ [#981](https://github.com/rime/weasel/issues/981)

---

## 设计启示汇总

**核心判定：三个目标——"被动观察所有应用输入框的最终文本（含 IME 上屏中文）＋不吞键＋不影响用户现有中文 IME"——TSF 被动 TIP 路径存在结构性冲突，不能整体成立。**

1. **"注册即被动加载"不存在**（官方文档两处原句＋本机 build 26300 实测：启用未选中 → 零进程加载）。未选中就没有 `Activate()`，没有 `Activate()` 就无法 advise `ITfTextEditSink`，拿不到任何编辑事件。Praetorian 的说法在本机实测下不成立。
2. **键盘类别 per-language 互斥**（官方答复确证）：要成为活动 TIP 就必须顶掉用户的微软拼音/搜狗——直接违背"不影响输入"。
3. **非键盘类别并存是未验证假设**：`GUID_TFCAT_TIP_OTHER` 在现行 SDK 头文件与官方类别文档中都不存在；`ActivateProfile` 无类别参数；无系统自动激活非键盘 TIP 的公开先例。若坚持 TSF，唯一可试的实验是注册 SPEECH/HANDWRITING 类别 profile 并自调 `ActivateProfile` 看能否与拼音并存——存疑，不应作为方案基线。
4. **宿主覆盖面天然残缺**（官方答复确证）：经典 EDIT（大量传统 Win32 程序的输入框）是 TRANSITORY context，直接键入完全不可见；RichEdit 默认关闭 TSF。可观察的是 TSF-aware 宿主（Chromium 系浏览器、新版 Notepad、Word、开了 SES_USECTF 的 RichEdit）。"所有应用输入框"这个目标 TSF 路径覆盖不了。
5. **删除内容必须影子副本差分**；IME 上屏文本在 commit 编辑会话中经 `GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT)`＋`ITfRange::GetText` 可读（TSF-aware 宿主内）。
6. **若仍要落地 TSF 观察器**（接受上述边界）：签名可不做（weasel 实证未签名可用）但建议至少自签＋本地信任以过 Secure Mode/UWP/Smart App Control；注册走 weasel 模式（regsvr32 自注册 → HKCR CLSID → RegisterCategory → RegisterProfile(langid=0x0804, bEnabledByDefault=TRUE)）；**卸载务必先杀 ctfmon 再删键**（回写复活教训）；先 `ITfContext::GetStatus` 查 `TS_SS_TRANSITORY` 跳过盲宿主。
7. **现实替代**：微软官方答复点名 UI Automation 文本变化事件是无 text store 宿主的替代观察路径；另一个 TSF 内的次优解是"双语言槽位"——observer 注册为 en-US 的键盘 TIP（与 zh-CN 拼音并存，官方答复者实测可行），但只能观察英文直接输入场景，**中文上屏依然不可见**。综合结论：若"含 IME 上屏中文＋全部应用＋零干扰"三条都必须满足，被动 TSF TIP 方案判定为不可行，应转向 UIA/混合方案或接受收窄后的边界。

探针与快照留存：`temp_scripts/weasel_probe/`（安装前/后注册表快照 .reg、键名列表、cleanup_weasel.ps1）；系统已恢复安装前状态（注册表逐键比对一致，文件全部清除，下载的安装包已删除）。
