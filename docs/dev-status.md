# Tinta 开发状态台账

> 这是唯一记录“事实 / 决定 / 实现 / 验证 / 暂缓”的地方。
> 状态标记见 [README.md](README.md)。设计原理不要写在这里，只写结论和证据。

## A. 当前事实（可能是缺陷）

| ID | 事项 | 状态 | 事实 | 证据 |
| --- | --- | --- | --- | --- |
| F01 | Home 按 Esc 会退出软件（P0 前） | `[已废弃]` | 已修复：Home/Picker/阅读页的 Esc 都只 `Dismiss`，不再 `WM_CLOSE` | `tests/validate_esc_never_quits.ps1` 4/4 通过；`out/esc-never-quits` |
| F02 | 有仓库启动会丢 session（P1 前） | `[已废弃]` | 已修复：Home 固定在 0，session 文件恢复到右侧；关闭后 `tab=` 保留 | `tests/validate_home_tab_session.ps1` pass 1；`out/home-tab-session` |
| F03 | Home 按 `N` 会覆盖 Home（P1 前） | `[已废弃]` | 已修复：Home 按 N 打开快速笔记标签，Home 保留；Home 内新建文件流程仍未做 | 同一脚本的“N opens a note tab”项 |
| F04 | 快捷键自定义只支持单键 | `[现状]` | `KeyBinding` 只有 `{key,isChar}`，无 Ctrl/Shift | [keymap.h](../include/keymap.h#L11-L14)、[input.cpp](../src/input.cpp#L3639-L3657) |
| F05 | Alt 组合被统一拒绝 | `[现状]` | `shortcutModifiersAllowed` 检测到 Alt 直接 false | [keymap.cpp](../src/keymap.cpp#L5-L15) |
| F06 | Ctrl+Shift 字母白名单只有 S/N/V/E/F | `[现状]` | 仅放行 S/N/V/E/F + 导航键 | [keymap.cpp](../src/keymap.cpp#L5-L15) |
| F07 | `Ctrl+P` 是打印预览 | `[现状]` | 阅读态、编辑态都占用 | [input.cpp](../src/input.cpp#L3875-L3881)、[editor.cpp](../src/editor.cpp#L2735-L2739) |
| F08 | `B` 是文件夹浏览器 | `[现状]` | 不是快速跳转 | [input.cpp](../src/input.cpp#L4091-L4109) |
| F09 | `Ctrl+Shift+E` 是全宽阅读视图 | `[现状]` | Esc 回源码，不是回阅读页 | [editor.cpp](../src/editor.cpp#L2541-L2548)、[editor.cpp](../src/editor.cpp#L2740-L2744) |
| F10 | 快速跳转不存在（P2b 前） | `[已废弃]` | 已实现：`Ctrl+K` 浮层、输入/结果/键鼠、后台索引、最近空状态、文件夹跳转 | `tests/validate_quickjump.ps1` 19/19；`tests/quickjump_tests.cpp` |
| F11 | 全仓库索引不存在 | `[现状]` | 搜索只扫当前文件同级目录，最多 400 个 | [search.cpp](../src/search.cpp#L124-L174) |
| F12 | Home 文件操作不存在（P1b 前） | `[已废弃]` | 已实现：新建文件/文件夹、重命名、删除到回收站（带确认和校验） | `tests/validate_home_file_ops.ps1` 5/5；`out/home-file-ops` |
| F13 | 仓库重新定位/设为默认不存在 | `[已验证]` | Picker 中标记 `folder missing` 的行可按 `r` 重定位；按 `d` 设为启动仓库；`x` 两次确认才移除；保留 repo id/name，拒绝重复路径和缺失仓库 | `home_logic_tests`；用户手工重定位通过；`out/repo-default/*.png` |
| F14 | 设置写失败只覆盖部分路径 | `[现状]` | 仓库添加/设置页有提示；`persistLastDir` 等忽略返回值 | [homeview.cpp](../src/homeview.cpp#L147-L157)、[overlays.cpp](../src/overlays.cpp#L3216-L3223) |
| F15 | `j` 下、`k` 上 | `[现状]` | Home、Picker、阅读滚动一致 | [home_logic.cpp](../src/home_logic.cpp#L244-L248) |
| F16 | 非 md 不等于不可打开 | `[现状]` | `.txt`、`.mmd`、`.puml/.plantuml` 都是支持类型 | [document.cpp](../src/document.cpp#L103-L123) |

## B. 已决定（含实现中 / 已验证）

| ID | 事项 | 状态 | 决定 | 验收 |
| --- | --- | --- | --- | --- |
| D01 | Esc 永不退出 | `[已验证]` | Home/Picker/阅读页都只 `Dismiss`，绝不 `WM_CLOSE`；Q/关闭按钮/Alt+F4 仍退出 | `tests/validate_esc_never_quits.ps1` 4/4；`home_logic_tests` 覆盖空过滤/无仓库 Esc |
| D02 | `j=下、k=上` | `[已决定]` | 保持现状，不改代码；方案文字统一 | 文档不再出现反向描述 |
| D03 | V1 包含基础文件操作 | `[已验证]` | 单选：新建文件/文件夹、重命名、删除到回收站 | `tests/validate_home_file_ops.ps1` 5/5；移动/多选/撤销进 V1.5 |
| D04 | 首页固定最左标签（唯一 Home） | `[已验证]` | 打开文件不关闭 Home；Home 不可关闭/拖动/被批量关闭；`Ctrl+T` 激活 Home | `tests/validate_home_tab_session.ps1` 18/18；Home 在 0，文件标签在右 |
| D06 | 编辑页 Esc 单击回阅读 | `[已验证]` | 干净缓冲单次 Esc 回阅读；脏缓冲仍先弹保存询问 | 同一脚本的 `--edit` 单项：单次 Esc 后 Ctrl+W 能关标签 |
| D07 | 有仓库启动默认激活 Home | `[已验证]` | session 文件恢复到右侧，Home 激活；文件参数/单文档窗口暂不强制 Home | 同一脚本 pass 1；Home 标题不含 a.md/b.md |
| D08 | Session 合并（Home + 文件标签） | `[已验证]` | Home 不写入 session；文件路径恢复；关闭最后一个文件回 Home | `validate_home_tab_session` 18/18 + `validate_home_startup_edges` 10/10（Picker、文件参数、openInTabs=false） |
| D09 | V1 包含 Quick Jump | `[已决定]` | 产品简报已确认 | 实现排 P2 |
| D10 | P2a 仓库索引与匹配 | `[已验证]` | `workspace_index` 扫描 + 纯 query | `tests/workspace_index_tests.cpp` 通过；标题/隐藏/截断/排序覆盖 |
| D11 | P2b `Ctrl+K` 浮层 | `[已验证]` | 固定 `Ctrl+K`；Enter 在当前窗口打开/激活标签（不新开窗口），Esc 关闭并回原 Base；后台扫描带 generation，过期结果丢弃；文件夹结果跳 Home | `tests/quickjump_tests.cpp`；`tests/validate_quickjump.ps1` 19/19 |
| D12 | P3 仓库重定位与启动仓库 | `[已验证]` | Picker 选中 `folder missing` 行按 `r` 重定位；按 `d` 把选中仓库设为启动仓库；保留 id/name，重复路径和缺失仓库拒绝 | `home_logic_tests`；用户手工重定位通过；默认仓库写入 settings 并显示 default 标记 |
| D13 | P3 移除仓库二次确认 | `[已验证]` | `x`/Delete 第一次只提示，同一行再按一次才遗忘；移动/打开/点击会取消确认 | `home_logic_tests` 覆盖首次 arm 与二次 remove |
| D14 | Home 内联新建/重命名 | `[已验证]` | 新建文件/文件夹在列表顶部显示临时行；重命名在行内编辑；Enter 提交、Esc 取消；删除仍用确认框 | 探针验证新建/重命名/取消；`out/inline/*.png` |
| D15 | Home 剪贴板模型 | `[已验证]` | `Ctrl+A` 选全列，`Ctrl+C/X` 复制/剪切选中项，`Ctrl+V` 粘贴到当前目录；同名默认跳过、不覆盖；移动文件夹禁止进入自身；粘贴结果只保留到下一条命令 | `home_logic_tests`；探针验证复制、剪切与冲突跳过；`out/clipboard/*.png` |
| D16 | Home 多选删除 | `[已验证]` | `D`/`Delete` 对选中项生效；确认框显示 `Move N items…`；全部送回收站，成功/失败计数；无选择时仍删当前项 | `home_logic_tests`；探针删除 a/b、保留 c；`out/delete/*.png` |

## C. 提案 / 待决定

| ID | 事项 | 状态 | 待决定 | 备注 |
| --- | --- | --- | --- | --- |
| P04 | Enter 是否跟随 `openInTabs` | `[提案]` | 建议 Enter 永远标签；Shift+Enter 新窗口 | 需要新的 spawn helper |
| P06 | 单文件启动是否带 Home | `[提案]` | 当前文件参数窗口不强制 Home；后续再定 | 影响标签栏外观 |
| P07 | 快速跳转用固定 `Ctrl+K` | `[已验证]` | `B` 保留给文件夹浏览器，`Ctrl+P` 保留打印 | Home/阅读/编辑都能打开；Esc 回原状态；`validate_quickjump` 覆盖三种 Base |

## D. 暂缓

| ID | 事项 | 状态 | 原因 |
| --- | --- | --- | --- |
| L01 | 文件移动/拖拽/多选/批量重命名 | `[暂缓]` | V1.5；需要选择模型和撤销 |
| L02 | 仓库管理页/重新定位/设为默认 | `[暂缓]` | P3；依赖 Home 固定标签 |
| L03 | `Ctrl+Shift+O` | `[暂缓]` | 需白名单 + `Ctrl+O` 的 `!shift` guard |
| L04 | 组合键自定义 | `[暂缓]` | P4；需扩展 `KeyBinding`、ini、捕获 UI、冲突检测 |
| L05 | 全仓库索引缓存 | `[暂缓]` | P2 先做内存索引，持久化缓存以后再说 |
| L06 | 标签溢出时 Home 始终可见（pin 出滚动区） | `[暂缓]` | 当前 Home 在 index 0，但标签很多时 tabFirstVisible 可能把它滚出视野 |

## E. 已验证

| ID | 事项 | 状态 | 验证方式 |
| --- | --- | --- | --- |
| V01 | Home 列宽/行样式优化 | `[已验证]` | `home_logic_tests`、`i18n_tests`、`tests/validate_home_columns.ps1` 11 张截图 0 失败 |
| V02 | Home 布局回归规则 | `[已验证]` | `home_logic_tests` 覆盖 800/900、520、400、窄窗填充 |
| V03 | Esc 会退出 / session 被清空 / N 变快速笔记（P0 前） | `[已验证]` | 三个 `out/*-probe` 实测 |
| V04 | Esc 永不退出（P0） | `[已验证]` | `home_logic_tests`；`tests/validate_esc_never_quits.ps1` 的 Home/Reader/Picker 三项 |
| V05 | 显式退出仍可用 | `[已验证]` | 同一脚本的 `Q` 项：进程正常退出 |
| V06 | Home 固定标签 + session 恢复 | `[已验证]` | `tests/validate_home_tab_session.ps1` 18/18；Home 在 0，a.md/b.md 在右 |
| V07 | 编辑页单次 Esc 回阅读 | `[已验证]` | 同一脚本的 `--edit` 单项：Esc 一次后 Ctrl+W 关闭标签 |
| V08 | 阅读页 Esc 回 Home | `[已验证]` | 同一脚本的“reader Esc returns to Home”项 |
| V09 | Home 新建/重命名/删除 | `[已验证]` | `tests/validate_home_file_ops.ps1` 5/5：新建文件、Shift+N 文件夹、重命名、回收站删除、重名错误 |
| V10 | Home 文件对话框中文/IME | `[实现中]` | 候选窗/组合窗定位到名称字段光标；`WM_IME_COMPOSITION` 的 `GCS_RESULTSTR` 直接插入。`D:\测试` 下中文文件名通过 Unicode 消息验证 | 真实输入法的候选窗位置和中文输入需人工目测；原生 IME 路径尚未自动化 |
| V11 | P1 启动边角 | `[已验证]` | Picker 启动 + session、文件参数文档窗口、`openInTabs=false` 会停靠 | `tests/validate_home_startup_edges.ps1` 10/10 |
| V12 | P2a 索引纯逻辑 | `[已验证]` | 标题提取、文件名/标题/路径匹配、多词 AND、最近文件、隐藏/截断/扫描失败 | `tests/workspace_index_tests.cpp` 通过 |
| V13 | P2b Quick Jump 浮层 | `[已验证]` | 输入/结果/键鼠；Enter 打开；Esc 回原 Base；最近空状态；索引中/索引错误；窄窗；文件夹跳转；CJK；后台 generation | `tests/quickjump_tests.cpp`；`tests/validate_quickjump.ps1` 19/19；`out/quickjump/*.png` |
| V14 | Quick Jump IME 路径 | `[实现中]` | 组合窗定位到查询光标，`GCS_RESULTSTR` 批量写入并吞掉重复 WM_CHAR | 代码路径与 Home 对话框一致；真实输入法候选窗位置仍需人工目测 |
| V15 | P3 仓库重新定位 | `[已验证]` | 缺失仓库 → Picker → `r` → 选择新目录 → Home 直接打开新路径；重复路径拒绝 | 用户手工验收通过；`home_logic_tests`；`out/repo-relocate/*.png` |
| V16 | P3 启动仓库 | `[已验证]` | Picker 按 `d` → currentRepoId=选中、startupMode=last、settings 落盘、行尾显示 default；缺失行拒绝 | 探针读取 settings：`repoCurrent=r1`、`startupMode=0`；`out/repo-default/*.png` |
| V17 | P3 移除仓库二次确认 | `[已验证]` | `x` 第一次提示 “Press X again…”，第二次才 forget；不改磁盘文件夹 | `home_logic_tests` 通过 |
| V18 | Home 内联编辑 | `[已验证]` | 新建临时行、原地重命名、Enter/Esc、错误底部提示、取消不落盘 | `out/inline/01-03*.png`；探针检查文件系统结果 |
| V19 | Home 剪贴板 | `[已验证]` | Ctrl+A 全选、Ctrl+C/X 复制/剪切、Ctrl+V 粘贴、同名跳过、剪切后源消失、粘贴提示在下一次按键时清除 | `out/clipboard/*.png`；探针检查磁盘文件；`home_logic_tests` 覆盖提示清除 |
| V20 | Home 多选删除 | `[已验证]` | 选中 2 项按 D → 确认 `Move 2 items…` → Enter 后两项进回收站，未选中项保留 | `out/delete/*.png`；探针检查文件系统；`home_logic_tests` 覆盖 dialogTargets |

## F. 环境限制

| ID | 事项 | 状态 | 说明 | 证据 |
| --- | --- | --- | --- | --- |
| E01 | 低完整性/沙箱进程写仓库被拒 | `[现状]` | 该进程 token 的完整性级别为 `S-1-16-4096`（Low），`CreateFileW` 对 `D:\测试` 返回错误 5；同一 exe 以普通 Medium 进程启动可写 | 进程 token 检查；`D:\测试\测试.md` 正常启动创建成功 |

## G. 最近更新

- 2026-10-19：建立文档索引与状态标记；收敛状态机术语（去掉“三个正交维度”），把命令/焦点归到状态机，把数据归属/索引归到工作区模型。
- 2026-10-19：完成 P0（Esc 永不退出）。Home/Picker/阅读页消费 Esc；Q 仍退出；新增 `tests/validate_esc_never_quits.ps1`。
- 2026-10-19：完成 P1 核心（Home 固定标签 + session 合并 + 阅读页 Esc 回 Home + 编辑页单次 Esc）。新增 `tests/validate_home_tab_session.ps1`，18/18 通过；Picker/文件参数/`openInTabs=false` 边角待补测。验证用的是 `build/Release/tinta-preview.exe`，因为用户正在使用的 `tinta.exe` 占用了文件。
- 2026-10-19：完成 P1b（Home 新建文件/文件夹、重命名、删除到回收站），带校验、冲突提示和确认框。新增 `tests/validate_home_file_ops.ps1`，5/5 通过。
- 2026-10-19：文件操作失败时显示 Win32 错误码；名称校验额外拒绝零宽/格式字符（C0/C1、U+200B–200F、U+202A–202E、U+2060–206F、FEFF、00AD），避免“看起来正常但系统拒绝”的名字。
- 2026-10-19：Home 文件对话框接入 IME：`WM_IME_STARTCOMPOSITION/COMPOSITION` 把候选窗锚到字段光标；`GCS_RESULTSTR` 直接写入名称字段并吞掉随后的 WM_CHAR。中文文件名在 `D:\测试` 通过 Unicode 消息验证；真实输入法位置待目测。
- 2026-10-19：错误 5 定位为**低完整性/沙箱进程**：截图中的 Tinta 进程 integrity 为 `S-1-16-4096`，连 ASCII 名字也无法写 `D:\测试`；同一 exe 以普通进程启动可创建。应用侧已把错误 5 改为“Launch Tinta outside the sandbox / 允许写入该文件夹”的明确提示。
- 2026-10-19：补 P1 边角：`tests/validate_home_startup_edges.ps1` 10/10（Picker + session、文件参数文档窗口、openInTabs=false 会话停靠）。
- 2026-10-19：P2a 落地 `workspace_index`：递归扫描（隐藏/.git/node_modules/reparse/maxEntries）、H1 标题、纯 query（文件名/标题/路径、多词 AND、最近文件），`tests/workspace_index_tests.cpp` 通过；P2b 再接 Ctrl+K 浮层。
- 2026-10-19：P2b 落地 `Ctrl+K` Quick Jump：输入框、结果列表、键盘/鼠标、空查询最近文件、索引中/错误、文件夹跳 Home、Enter 在当前窗口打开/激活标签、Esc 回原 Base。索引器独立到 `quickjump_indexer`，每次打开递增 generation，过期结果丢弃；纯决策在 `quickjump_logic`。`tests/quickjump_tests.cpp` 4 组通过，`tests/validate_quickjump.ps1` 19/19；截图见 `out/quickjump`（正常/窄窗、最近空、索引中、索引错误、CJK、编辑态）。
- 2026-10-20：修复 Quick Jump 真实输入与长文件名：命令键返回 `true`、可打印键返回 `false`，恢复 `TranslateMessage → WM_CHAR`；结果名/路径改用省略号 trimming，查询框加裁剪。验证脚本改走 `WM_KEYDOWN → TranslateMessage`，新增超长文件名 fixture，19/19。
- 2026-10-20：P3 开始——缺失仓库重新定位。Picker 的 `folder missing` 行按 `R` 打开目录选择框，更新 path/lastDir/currentRepoId 并保留 id/name；重复路径红字拒绝。`home_logic_tests` 通过；手工选目录验收待做。
- 2026-10-20：P3 完成——Picker 按 `d` 设为启动仓库（currentRepoId + startupMode=last），行尾显示 `default`；缺失仓库拒绝。`home_logic_tests` 通过；探针确认 settings 与默认标记。用户已手工验收重定位。
- 2026-10-20：P3 收尾——`x`/Delete 移除仓库改为二次确认，首次只提示且移动/打开/点击会取消；`home_logic_tests` 通过。
- 2026-10-20：Home 新建/重命名改为内联编辑：新建在列表顶部显示临时行，重命名在行内替换名字；Enter 提交、Esc 取消，错误显示在底部；删除仍保留确认框。探针验证新建/重命名/取消与磁盘结果。
- 2026-10-20：Home 剪贴板模型：Ctrl+A 全选、Ctrl+C/X 复制/剪切、Ctrl+V 粘贴；同名跳过不覆盖，剪切成功后清空剪贴板，跨目录/文件夹由 SHFileOperation 处理。探针验证复制 2 个文件、剪切 1 个文件。
- 2026-10-20：修复粘贴提示滞留：`Pasted …` 只保留到下一次按键；之后复制/移动/选择会重新显示 `N copied` / `N selected`。
- 2026-10-20：Home 多选删除：`D`/Delete 把 `dialogTargets` 填成选中项，确认框显示 `Move N items…`，批量送回收站并报告成功/失败；探针删除 a/b 后 c 保留。
