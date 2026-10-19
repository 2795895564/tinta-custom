# 工作区数据、命令路由与索引契约

> 状态：`[提案]`（数据归属和路由契约的目标形态；当前代码状态见 [dev-status.md](dev-status.md)）
>
> **唯一职责**：命令路由机制、工作区数据归属、Effect 契约、索引契约。
> **不负责**：界面状态、焦点、命令表、状态转换（见 [interaction-state-machine.md](interaction-state-machine.md)）。
> **不变量与模块边界**：见 [design-constitution.md](design-constitution.md)。

## 0. 三个文档的边界

```text
design-constitution.md       原则 / 不变量 / 模块边界
interaction-state-machine.md 状态 / 焦点 / 命令表 / 转换
input-workspace-model.md     数据归属 / 路由机制 / Effect / 索引
```

命令表只在状态机里定义；本文只说“路由器怎么把事件变成命令”，不重复命令清单。

## 1. 数据归属

| 状态 | 归属 | 生命周期 | 持久化 |
| --- | --- | --- | --- |
| 仓库列表 | `Settings` / `repos` | 跨进程 | `settings.ini` |
| 当前仓库、当前目录、选择、列快照 | `WorkspaceState`（现 `HomeState`） | 每个窗口 | 仅 `lastDir` |
| 打开的标签、活动标签 | `Tabs` | 每个窗口 | session |
| 当前文档内容、caret、undo、dirty | 活动 tab / `DocumentState` | 每个窗口 | 文件本身 + 草稿 |
| 快速跳转 query/results/selection | `QuickJumpState` | 浮层打开期间 | 否 |
| 仓库索引 | `IndexService` | 每个仓库/窗口 | 可选缓存 |
| 主题、语言、键位、启动方式 | `Preferences` | 跨进程 | `settings.ini` |

规则：

- **一个状态只有一个所有者。** Home/Picker/QuickJump 不各自保存一份仓库或目录。
- `WorkspaceState` 是 Home、Picker、QuickJump 的共同数据源；QuickJump 是读者，不是所有者。
- `Tabs` 只拥有文档身份和缓冲，不拥有仓库、目录、索引。
- `Preferences` 只拥有用户取舍，不拥有 session/运行时状态。

## 2. 命令路由契约

### 2.1 路由器职责

`InputRouter` 只做三件事：

1. 读取事件（Text / CommandChord / Mouse / Wheel）；
2. 结合 `InteractionState` 和键位表，解析出 `Command`；
3. 把命令交给 `dispatchCommand`，或者返回“未消费”。

它**不**执行具体行为：不打开文件、不写设置、不绘制。具体行为由 command handler → `Effect` 完成。

### 2.2 伪代码

```text
route(event, state):
    if modal consumes(event): return
    if overlay consumes(event): return

    if event is Text:
        if textFocus: text.handle(event); return
        if base.handleText(event): return

    if event is CommandChord:
        if textFocus and textClaims(event): text.handle(event); return
        command = keymap.resolve(event, state.focus)
        if command: dispatch(command); return
        if base.handleChord(event): return

    // 未消费：什么都不做，不要 Quit
```

优先级、Text/Chord 的分类、命令表都在 [interaction-state-machine.md](interaction-state-machine.md) 第 3–5 节。这里只固定路由器的契约。

### 2.3 键位模型

目标结构（P4）：

```text
KeyChord { ctrl, shift, alt, key, isChar }
Binding  { commandId, context, chord, profile, override }
```

第一期不实现完整组合键自定义：

- `Ctrl+K`（QuickJump）、`Esc`（Dismiss）固定；
- 现有单键 profile 继续可用；
- `Ctrl+Shift+O` 需要等白名单和 `Ctrl+O` guard 一起改。

## 3. Effect 契约

纯逻辑不直接开窗口、写设置、发通知。它返回 effect，shell 执行。

```text
WorkspaceState + Command
        │
        ▼
workspaceApply() → vector<Effect>
        │
        ├─ ScanDir(path, slot, generation)
        ├─ ScanRepo(repoId, generation)
        ├─ OpenTab(path)
        ├─ ActivateTab(id)
        ├─ SaveFile(path, content)
        ├─ CreateEntry / RenameEntry / RecycleEntry
        ├─ SaveSettings(changes)
        ├─ ShowNotice(level, text)
        └─ CloseWindow
```

| Effect | 执行者 | 失败反馈 |
| --- | --- | --- |
| `ScanDir` / `ScanRepo` | 工作线程 | 列表显示错误；旧结果按 generation 丢弃 |
| `OpenTab` | `tabs.cpp` | 状态栏/提示；不进入文档 |
| `SaveFile` | 编辑器/文件服务 | 保存询问/错误提示 |
| `Create/Rename/Recycle` | 文件服务（shell） | 行内或底部错误；不刷新成成功 |
| `SaveSettings` | `settings.cpp` | 红字 + 路径，不静默 |
| `ShowNotice` | 信号/状态栏 | — |

现有 `home_logic` 的 `pendingOpen`、`need`、`pendingBrowse`、`pendingChoose`、`pendingRemove` 就是 effect 的雏形，保留并逐步统一。

P1b 已实现 Home 的 `Create / Rename / Recycle`：纯逻辑负责对话框、校验和提交标记，`homeview.cpp` 的 `homeCommitDialog` 执行文件系统操作；错误留在对话框里。`ERROR_ACCESS_DENIED` 优先按“低完整性/沙箱进程”提示，而不是当成名称错误。

## 4. 索引契约

```text
IndexService
├─ repoId / root
├─ status: Idle | Scanning | Ready | Error
├─ files: path, name, kind, size, mtime
├─ generation
└─ query(text, limit) → ranked paths
```

规则：

- 后台递归扫描，只收受支持的文档类型；
- 忽略隐藏目录和 `.git`；
- 每次仓库切换/文件变化递增 generation，过期结果丢弃；P2b 在每次打开浮层时递增，仓库内容变化靠下一次打开刷新（首期无文件 watcher）；
- QuickJump 只调用 `query`，不自己扫盘；
- 首期不持久化缓存，启动后按需构建；
- 扫描中 QuickJump 可显示部分结果，但必须标“正在索引”；
- 错误只进入 `status=Error`，不阻塞 UI。

P2a 已落地 `workspace_index.h/.cpp`：`scan()` 是唯一的文件系统入口（跳过隐藏项、`.git`、`node_modules`、reparse point，带 `maxEntries`/`truncated`），`query()` 是纯函数（文件名/标题/相对路径、多词 AND、最近文件排序）。`tests/workspace_index_tests.cpp` 覆盖标题提取、匹配排序、隐藏/截断/扫描失败。

P2b 已落地 `Ctrl+K`：`quickjump_indexer` 在后台运行单线程扫描，每次请求带 generation，`quickjump_logic::resultIsCurrent` 同时校验 generation 与 root，过期结果不写入当前索引；`quickjump` 只调用 `query()`，不在视图里扫盘。最近文件是空查询的 mtime 排序，索引为空时显示“还没有最近文件”；扫描中保留旧缓存结果并标“正在索引”。`tests/quickjump_tests.cpp` 覆盖 generation 丢弃、列表移动和空状态分支。

## 5. 持久化边界

| 数据 | 路径 | 写入时机 |
| --- | --- | --- |
| 偏好（主题、语言、键位、启动方式） | `settings.ini` | 用户 Apply / 明确切换 |
| 仓库列表、lastDir | `settings.ini` | 添加/移除/离开仓库 |
| session tabs/active | `settings.ini` | 窗口关闭/会话变化 |
| 草稿 | 配置目录 | 崩溃恢复/自动保存 |
| 索引 | 内存（首期） | 不持久化 |

规则：

- 视图不直接 `loadSettings()/saveSettings()`；通过 `SettingsStore` / `applyPreferences`。
- 一次操作只写一次设置；失败返回给调用方显示。
- session 与 preferences 分开；不要把“打开过的标签”当设置。

### 5.1 Session 合并设计（P1，核心已实现）

> 状态：`[已验证]`。核心路径由 `tabsSeedWorkspace` / `tabsEnsureHome` /
> `homeActive` / 标签关闭保护实现；`tests/validate_home_tab_session.ps1` 18/18，
> `tests/validate_home_startup_edges.ps1` 10/10（Picker 启动 + session、
> `openInTabs=false` 会话停靠、文件参数文档窗口）。文件参数 + `openInTabs=true`
> 仍按单实例加入现有窗口设计，不在 Home 合并范围内。

目标：**Home 固定在 index 0，文件标签恢复到右侧；不再因为仓库启动而丢 session。**

#### 数据模型

```text
DocTab {
  int id;
  bool home;          // 新增：index 0 的固定首页
  string path;        // home = true 时为空
  string title;       // home 显示 "Home"
  ...
}
```

session 只保存**文件路径**，不保存 Home（Home 每次启动重建）。Home 不是“打开过的文档”，不进 session 文件列表。

#### 启动规划（P1 已实现；集成测试覆盖）

```text
planWorkspaceSession(savedTabs, savedActive, hasRepo, launchPath):
    files = dedupe(existing(savedTabs))        // 保留顺序，丢掉不存在/重复
    tabs = []
    if hasRepo: tabs.push(Home)
    if launchPath: ensure launchPath in files
    tabs += files
    if hasRepo:
        active = Home                           // 默认回到 Home
        if launchPath: active = launchPath      // 文件参数优先
    else:
        active = launchPath if launchPath else clamp(savedActive, files)
    return { tabs, active }
```

规则：

- **有仓库、无文件参数**：Home 激活；session 文件标签恢复到右侧；没有 session 就只显示 Home。
- **有仓库、有文件参数**：Home 在 0，文件参数激活；session 文件标签仍恢复（与现有行为一致）。
- **启动方式 = 仓库选择页**：仍恢复 session 文件标签，Home 标签显示 Picker；选完仓库后文件标签已经在右侧，且不会因为没选仓库而丢 session。
- **当前仓库路径丢失**：Home 标签显示 Picker + 红字；session 文件标签仍恢复，路径独立的文件不丢。
- **无仓库**：不创建 Home；沿用现有 session 恢复/启动页。
- **`openInTabs=false`**：session 不恢复（保持现有语义）；主窗口 Home，只有文件参数窗口例外；卫星窗口不创建 Home。
- **缺失文件**：从 `files` 中丢弃；如果 active 指向缺失文件，回 Home。
- **重复路径**：大小写不敏感去重，保留第一次出现的位置。
- **旧 session**：没有 Home 字段；有仓库时在 0 前插 Home；旧 `sessionActive` 映射到过滤后的文件下标。
- **dirty 缓冲**：session 只恢复路径，不恢复未保存内容；脏缓冲靠草稿/保存询问，不允许静默丢。

#### 保存规划

- 只写有路径的文件标签；Home 不写。
- `sessionActive` 仍表示“文件标签中的下标”；有仓库启动时忽略它（默认 Home），无仓库时用它恢复。
- 如果以后要“恢复上次文件而不是 Home”，增加 `sessionActiveHome` 或 `sessionActivePath`，不改文件列表格式。
- 设置写失败要显示，不得假装保存成功。

#### 标签不变量

- Home 永远 index 0；`tabOpenPath` 插入位置不得小于 1。
- Home 不可关闭、不可拖动、不可被“关闭其他/左侧/右侧”删除。
- 关闭最后一个文件标签 → 激活 Home，不把 Home 变成普通空标签。
- `Ctrl+T` → 激活 Home，不创建第二个首页；`+` 按钮仍可新建快速笔记。
- 只有 Home 一个标签时，标签栏可隐藏（沿用单标签标题栏），文件打开后再显示。
- `tabCycle` / tab switcher 包含 Home。
- `homeActive` 改为检查当前 tab 的 `home` 标志，而不是“任何空文档”；无仓库的旧启动页保留兜底。

#### 需要改动的现有路径

| 路径 | 变化 |
| --- | --- |
| `homeStartup` 分支 | 不再压过 session；改为 `planWorkspaceSession` |
| `tabsSeedSession` | 支持 Home + 文件标签，或换成 `tabsSeedWorkspaceSession` |
| `tabsInit` | 有仓库时确保 Home 在 0 |
| `openFromHome` / `startPageInvoke` | 不再关闭 shell 标签 |
| `tabCloseIndex` | Home 不可关；最后一个文件关闭后激活 Home |
| `tabOpenStartPage` | 唯一 Home：激活 Home，不新建空首页 |
| 标签拖动/右键/批量关闭 | 跳过 Home |
| session 保存 | 跳过 Home，保留文件路径和 active |
| `homeActive` | 检查 `DocTab.home` |

P1 已落地：`tabsSeedWorkspace`、`tabsEnsureHome`、`homeActive`、`tabCloseIndex` /
拖动 / 批量关闭的 Home guard、`openFromHome`/最近文件不关 shell、`Ctrl+T` 激活 Home。

#### 验收

1. 仓库 + 2 个 session 文件启动：3 个标签，Home 在 0 且激活；文件标签在右侧。
2. 关闭最后一个文件：回到 Home，标签栏隐藏，进程不退出。
3. 关闭 Home：无效果，不能把 Home 关掉。
4. `Ctrl+T`：激活 Home，不出现第二个“空首页”。
5. 关窗重开：Home + 文件标签还在；`settings.ini` 不再被清空。
6. 文件参数启动：Home 在 0，文件标签激活；缺失文件回 Home 并提示。
7. `openInTabs=false`：行为与现有单文档窗口兼容；卫星窗口没有 Home。

#### 验证

`tests/validate_home_tab_session.ps1` 当前覆盖（18/18）：

- 仓库 + 2 个 session tab 启动：Home 激活，文件标签在右。
- 关窗后 `tab=` 行保留，session 不被清空。
- Ctrl+Tab 遍历；Ctrl+W 关闭文件回 Home；Home 上 Ctrl+W 无效。
- 从 Home 打开文件不关闭 Home；阅读页 Esc 回 Home。
- 编辑页单次 Esc 回阅读（`--edit` 启动）。
- `Ctrl+T` 激活 Home；Home 上 `N` 打开快速笔记标签，不覆盖 Home。

待补测：

- 启动方式 = Picker 时的 session 恢复。
- 文件参数启动 + repo。
- `openInTabs=false` 的主窗口/卫星窗口。
- 标签拖动/右键“关闭其他/左/右”对 Home 的保护（代码已加 guard，需 UI 回归）。
- 中文 `tab.home` 标题。

## 6. 工作区数据不变量

- 一个窗口只有一个 `WorkspaceState`；Home/Picker/QuickJump 共读。
- `WorkspaceState` 的 `dir` 永远在 `root` 内。
- 一个文档路径最多对应一个 tab；激活 tab 的 `currentFile` 是唯一当前文档。
- 文件操作必须先有 selection，失败不得假装成功。
- 索引/扫描结果必须带 generation。
- 视图只读数据；写操作通过 Command/Effect。

## 7. 和现有代码的映射

| 现有 | 目标 | 状态 |
| --- | --- | --- |
| `HomeState` | `WorkspaceState` | `[现状]`，先扩展不改名 |
| `home_logic` | workspaceApply + effects | `[现状]`，已有 pending/need |
| `input.cpp` 长 if/else | InputRouter + dispatchCommand | `[提案]`，P1/P2 逐步收薄 |
| `keymap.*` | 命令绑定表 | `[现状]`，P4 扩展 KeyChord |
| `search.cpp` 同级扫描 | IndexService | `[提案]`，P2 |
| `settings.*` | SettingsStore + schema | `[现状]`，逐步加 validate/apply |
| `homeview.cpp` | Home 视图 + effect 执行 | `[现状]`，只画 + 命中 |

## 8. 待决定

- `HomeState` 是否改名为 `WorkspaceState`（建议 P1 后）。
- QuickJump 是否拥有独立的目录浏览状态，还是复用 `WorkspaceState` 的列。
- 索引是否需要持久化缓存（P2 先不做）。
- 设置 store 是否引入统一的 `applyPreferences`（P3 前）。
