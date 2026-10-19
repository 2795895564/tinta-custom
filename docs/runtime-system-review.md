# Tinta 运行系统方案 · 现状核对与差距分析

> **性质**：某次源码核查的快照，`[已验证]` 证据为主；建议部分标 `[提案]`。
> **不作为长期规范**：当前事实/决定/实现状态以 [dev-status.md](dev-status.md) 为准。
> 快照时间：2026-10-19。核对“屏幕/状态、Esc、首页固定标签、快捷键、快速跳转、仓库管理”这套方案。
> 快照中的 P0 Esc 缺陷、P1 的 Home 固定标签/session 合并/编辑页双击 Esc 均已在后续实现中改变；
> 本文保留当时的证据，不再代表当前行为。当前事实以 [dev-status.md](dev-status.md) 为准。
> 你提到的三张流程图在这条消息里没有附上；以下分析基于文字方案和源码。图发来后可以再逐节点对一次。

## 0. 结论摘要

- `[已验证]` 你列的 6 条冲突，**5 条完全成立**；`j/k` 方向也确实是你说的 `j` 下、`k` 上，建议保持。
- 除了你列的 6 条，还有几处会直接影响方案落地：
  1. 有仓库时的启动会**跳过 session 恢复**，退出时还会把旧 session 覆盖成空。实测：带 2 个 session tab 启动后直接关闭，`settings.ini` 里的 `tab=` 行消失。
  2. 首页按 `N` 现在会掉进全局 newfile，直接把首页标签变成“未命名快速笔记”，不是“在首页新建文件”。
  3. 快速跳转的 `B` 和 `Ctrl+P` 都已被占用：`B` 是文件夹浏览器，`Ctrl+P` 是打印预览。
  4. `Ctrl+Shift+E` 当前是“编辑页的全宽阅读视图”，Esc 回到源码，不是“回只预览”；方案里的按键语义要改。
  5. “首页固定最左标签”与现有“任何空标签都是启动页”的模型冲突，不是只改 `openFromHome` 一行。
- `[已决定]` **P0 可以独立先做**：Esc 永不退出，首页/选择页消费 Esc。它不依赖首页固定标签，风险最低。

## 1. 六条冲突逐条核对

| # | 你的判断 | 核对结果 | 源码证据 | 备注 |
| --- | --- | --- | --- | --- |
| 1 | Esc 会退出软件，Home 受牵连 | **成立** | [input.cpp](../src/input.cpp#L4037-L4070)：非编辑态 Esc 的最后一级是 `PostMessageW(WM_CLOSE)`；[input.cpp](../src/input.cpp#L4002) 先调 `homeOnKeyDown`，Home 返回 false 就落进这条链 | 首页无搜索过滤时确实退出；选择页无 root 时也退出；编辑态走自己的 Esc 处理，不会直接退出 |
| 2 | Alt 组合被禁止 | **成立** | [keymap.cpp](../src/keymap.cpp#L5-L15)：`shortcutModifiersAllowed` 检测到 `VK_MENU` 直接 false | `Alt+↑/↓`、`Alt+Click` 是编辑器的单独手势，不属于快捷键系统；AltGr 也被这个门禁挡住 |
| 3 | 快捷键自定义只支持单键 | **成立** | [keymap.h](../include/keymap.h#L11-L14)：`KeyBinding` 只有 `{key, isChar}`；[input.cpp](../src/input.cpp#L3639-L3657) 捕获只接受 Tab/Space/F1-F12/A-Z/0-9 | `[Keys]` 的 ini 值、`parseKeyName`、`keyIniName`、`keyBindingMatches`、`translateActionKey` 都要一起扩展 |
| 4 | Ctrl+Shift+字母只有 S/N/V/E/F | **成立** | [keymap.cpp](../src/keymap.cpp#L5-L15)：Ctrl+Shift 只放行 S/N/V/E/F 以及 Tab、方向键、Home/End/PgUp/PgDn | 只把 O 加进白名单还不够：`Ctrl+O` 在 [input.cpp](../src/input.cpp#L3703-L3711) 会先打开文件对话框，必须同时加 `!shift` 判断 |
| 5 | `j` 下、`k` 上 | **成立** | [home_logic.cpp](../src/home_logic.cpp#L244-L248)：`DOWN/J` 向下，`UP/K` 向上；选择页 [home_logic.cpp](../src/home_logic.cpp#L205-L207) 相同 | 建议保持 `j=下、k=上`，与 vim/Yazi 及现有阅览滚动一致 |
| 6 | “默认新开窗口”有歧义 | **成立** | `openInTabs` 只影响外部文件启动和 session 恢复（[main_d2d.cpp](../src/main_d2d.cpp#L2227-L2237)）；Home 打开文件固定走 `tabOpenPath`，不读这个设置（[homeview.cpp](../src/homeview.cpp#L67-L73)） | 从 Home 打开文件永远是标签；要做“新窗口”需要新的 spawn helper，现成的 `launchDocumentWindow` 会强制 `--edit` |

### 额外实测 `[已验证]`

- `out/esc-probe`：用仓库启动 Home，向窗口投递 Esc，进程直接退出。
- `out/session-probe`：`[Session]` 写入 `tab=a.md`、`tab=b.md`，带仓库启动后直接关窗，退出后的 `settings.ini` 不再有 `tab=` 行。
- `out/home-key-probe`：Home 按 `N`，窗口标题变成 `Tinta - Untitled`，首页标签被快速笔记接管。

## 2. 当前软件实际是怎么运行的

### 2.1 启动

当前启动分支（[main_d2d.cpp](../src/main_d2d.cpp#L2260-L2293)）顺序是：

1. `--new` 快速笔记；
2. 有文件参数：打开文件；
3. `homeStartup`：加载空文档，交给 Home/Picker 决定；
4. 恢复上次 session；
5. 否则尝试启动目录的 `syntax.md`，没有就进启动页。

`homeStartup` 的条件是“配置了仓库或启动方式为仓库选择页”（[main_d2d.cpp](../src/main_d2d.cpp#L2230-L2237)）。它会刻意压过 session 恢复，理由是不让上次的文档盖住 Home。

**副作用**：session 文件仍在磁盘上，但本次没有把 `sessionPaths` 灌进 tabs；退出保存时只遍历当前 tabs，而当前只有一个空路径的 Home tab，于是旧 session 被清空。这是 P1 必须先修的隐藏问题。

`homeEnsureRoot` 在第一次绘制时决定打开上次仓库还是显示选择页（[homeview.cpp](../src/homeview.cpp#L543-L563)）。仓库文件夹不存在时，会显示选择页并保留红色提示。

### 2.2 屏幕和状态

| 屏幕 | 当前实现 | 与方案的关系 |
| --- | --- | --- |
| 启动页 / 仓库选择页 | `HomeScreen::Picker`，由 Home 渲染 | 已有；不是独立窗口或独立标签 |
| 首页（文件管理器） | `HomeScreen::Home`，Miller 三列 | 已有；布局刚更新 |
| 阅读页 | `editMode == false` 的文档视图 | 已有；没有单独的“预览”状态 |
| 编辑页 | `editMode == true`，源码 + 可选预览栏 | 已有 |
| 全宽阅读视图 | `editorReadingPreview`，`Ctrl+Shift+E` | 已有；是编辑缓冲的覆盖层，不是阅读页 |
| 快速跳转浮窗 | **不存在** | P2 全新 |
| 文件夹浏览器 / 搜索面板 / 目录 / 帮助 | 各类 overlay | 已有；Esc 会优先关它们 |

关键点：**“启动页”不是某个固定标签，而是“任何没有文件、也不在编辑态”的标签**（[startpage.cpp](../src/startpage.cpp#L372-L375)）。所以 `Ctrl+T` 新开的空标签也显示 Home/启动页，多个空标签会共享同一个 Home 状态。

### 2.3 标签模型

- `DocTab` 只有 `path / title / editMode / editorDirty / parked buffer` 等字段，没有“角色”或“固定首页”标记（[app.h](../include/app.h#L513-L527)）。
- `tabsInit` 在需要时创建一个空路径标签（[tabs.cpp](../src/tabs.cpp#L97-L107)）。
- 从 Home 打开文件：先 `tabOpenPath` 新建文件标签，再找到原来的 shell 标签并 `tabCloseIndex` 关掉（[homeview.cpp](../src/homeview.cpp#L67-L73)）。启动页最近文件也是同一套逻辑（[input.cpp](../src/input.cpp#L2531-L2539)）。
- 关掉最后一个标签时，不是关窗，而是把这个标签变成启动页（[tabs.cpp](../src/tabs.cpp#L243-L251)）。
- `Ctrl+T` 新开一个空启动页标签（[tabs.cpp](../src/tabs.cpp#L313-L335)）。
- 只有一个标签时标签栏不显示（[tabs.cpp](../src/tabs.cpp#L1449-L1452)）。
- session 只保存有路径的标签，空文件标签不持久化（[main_d2d.cpp](../src/main_d2d.cpp#L1825-L1838)）。

结论：**当前没有“永久首页标签”这个对象**。如果只做“打开文件后别关 shell 标签”，会得到一个留在最左的空标签；但要让它成为真正的固定首页，还需要解决角色、可关闭性、拖动、`Ctrl+T`、session 合并和单文件启动这些不变量。

### 2.4 Esc 实际链路

非编辑态（[input.cpp](../src/input.cpp#L4037-L4070)）：

```text
右键菜单/帮助/搜索/文件夹浏览器/目录/主题选择器/禅模式
  → 都不是：PostMessage(WM_CLOSE)
```

在进入这个 switch 之前，还有更早的捕获态：应用菜单、侧栏拖拽、灯箱、批注、创建引用、打印预览、标签切换器、标签拖动、标签菜单、设置、帮助、快捷键编辑等。它们各自消费 Esc。

编辑态（[editor.cpp](../src/editor.cpp#L2533-L2569)）：

```text
多光标 → 收起多光标
全宽阅读视图 → 回源码
脏缓冲 → 保存询问
干净缓冲 → 第一次 Esc 提示，500ms 内第二次 Esc 才退出编辑
```

Home 的 Esc 在通用 switch 之前：

- 搜索中：清空 query 并停止搜索（[home_logic.cpp](../src/home_logic.cpp#L229-L242)）；
- 首页有过滤但不在输入态：清空过滤（[home_logic.cpp](../src/home_logic.cpp#L274-L278)）；
- 首页无过滤：返回 false → 通用 switch → 退出；
- 选择页：有 root 时回 Home；没有 root 时返回 false → 退出（[home_logic.cpp](../src/home_logic.cpp#L213-L218)）。

与方案的差异：

- 方案要求“Esc 永不退出”，当前不是；
- 方案把禅模式放在编辑页之前，当前编辑态的 Esc 根本到不了通用 switch 的禅模式分支；
- 方案说“编辑页 → 只预览”，当前干净缓冲是**双击 Esc**，脏缓冲是保存询问；
- 方案里的“快速跳转浮窗”没有对象可关。

### 2.5 快捷键现状

`KEY_ACTIONS` 只有 13 个动作：search、browse、toc、theme、stats、quit、newfile、zen、scrollup、scrolldown、edit、help、annotate（[keymap.h](../include/keymap.h#L21-L29)）。默认 Windows/vim 两套，全部是单键或字符键。

关键占用：

| 键 | 当前行为 |
| --- | --- |
| `Q` | quit（可配置，`KA_QUIT`） |
| `E` | 进入编辑（默认 Windows 方案的 `KA_EDIT` 实际映射到 `:`） |
| `B` | 文件夹浏览器开关（**不是快速跳转**） |
| `Ctrl+P` | 打印预览（[input.cpp](../src/input.cpp#L3875-L3881)、[editor.cpp](../src/editor.cpp#L2735-L2739)） |
| `Ctrl+K` | 没有绑定（可作为快速跳转候选） |
| `Ctrl+Shift+E` | 编辑页全宽阅读视图（[editor.cpp](../src/editor.cpp#L2740-L2744)） |
| `Ctrl+E` | 编辑页显示/隐藏预览栏 |
| `Ctrl+T` | 新开启动页标签 |
| `Ctrl+W` / `Ctrl+F4` | 关标签 |
| `Ctrl+Shift+N` | 新窗口 |
| `Ctrl+O` | 打开文件对话框（没有判断 Shift） |

`shortcutModifiersAllowed` 是全局门禁：编辑器和主窗口都在最前面调用它。想加 `Ctrl+Shift+O`，不仅要把 O 加进白名单，还要把 `Ctrl+O` 的打开文件分支改成 `!shift`，否则会先弹文件对话框。

### 2.6 仓库管理与持久化

- 选择页已有：数字/方向键/`j k` 选仓库、Enter 打开、`o` / `Ctrl+O` 添加、`x` / Delete 从列表移除（只移除设置，不碰磁盘）。见 [home_logic.cpp](../src/home_logic.cpp#L189-L219) 和 [homeview.cpp](../src/homeview.cpp#L147-L175)。
- 启动方式（上次仓库 / 选择页）在 Settings 里，不在选择页（[overlays.cpp](../src/overlays.cpp#L2899-L2908)）。
- 离开仓库时会把当前 `H.dir` 写进 `repo.lastDir`，下次进入时恢复（[homeview.cpp](../src/homeview.cpp#L98-L127)）。
- 路径丢失：启动时能回到选择页并显示 `Folder not found: ...`（[homeview.cpp](../src/homeview.cpp#L548-L563)）；**没有“重新定位”**，也没有显式“设为默认”。
- `settings.ini` 写失败：添加仓库失败会在底部显示路径（[homeview.cpp](../src/homeview.cpp#L147-L157)）；设置页也有 `settingsSaveError` 红字（[overlays.cpp](../src/overlays.cpp#L3216-L3223)）。但 `persistLastDir`、`enterRepo` 等路径忽略 `saveSettings` 的返回值，失败时可能静默。
- 文件夹选择框的 `Show` HRESULT 没有被区分，取消和系统错误都返回空字符串（[homeview.cpp](../src/homeview.cpp#L177-L202)）。

### 2.7 搜索与“全仓库索引”

- `Ctrl+F` 是当前文档内搜索；
- `Ctrl+Shift+F` 是搜索结果面板；
- 后台的“文件夹搜索”只扫描当前文件的**同级目录**，最多 400 个文件，不会递归仓库（[search.cpp](../src/search.cpp#L124-L174)）；
- Home 的目录扫描只扫当前层，用 generation 丢弃过期结果（[homeview.cpp](../src/homeview.cpp#L46-L62)）；
- **没有全仓库 md 索引**。快速跳转浮窗要的“整个仓库模糊搜索”需要新数据结构和后台扫描。

### 2.8 边界现状

| 情况 | 当前状态 |
| --- | --- |
| 仓库文件夹启动时被删 | 选择页 + 红字提示，已有 |
| 仓库文件夹运行中被删 | Home 显示 `Can't read this folder`，不会自动回选择页 |
| `settings.ini` 写不进去 | 部分提示；部分路径静默 |
| 文件夹选择框取消/系统错误 | 都返回空，无错误码 |
| 超大仓库 | Home 当前层后台扫描，过期结果丢弃；全仓库索引不存在 |
| 非 md 文件 | **不是全部灰色**：`.txt`、`.mmd`、`.puml/.plantuml` 等是支持的文档类型，会正常打开；只有不支持的类型才变灰且不能打开（[document.cpp](../src/document.cpp#L103-L123)、[document.cpp](../src/document.cpp#L199-L209)） |
| 把文件夹拖进窗口 | Home/选择页会添加仓库，已有（[input.cpp](../src/input.cpp#L4382-L4405)） |

## 3. 方案与现状对照

| 方案项 | 现状 | 结论 |
| --- | --- | --- |
| Esc 永不退出 | 会退出 | **P0 可独立做** |
| 首页固定最左标签 | 无固定首页；空标签即启动页 | **P1，结构性改动** |
| Esc 预览回首页 | 无首页标签可回 | 依赖 P1 |
| 编辑页 Esc 回只预览 | 双击 Esc / 脏缓冲提示 | 需要 UX 决策 |
| 首页 `n/r/d` | 无；`N` 会变快速笔记 | P1b，新功能 |
| 快速跳转浮窗 | 不存在 | P2，全新 |
| 全仓库索引 | 只有同级目录搜索 | P2，全新 |
| 预览里 `B` 打开快速跳转 | `B` 是文件夹浏览器 | **冲突，建议换键** |
| 编辑里 `Ctrl+P` | 打印预览 | **冲突，建议 `Ctrl+K`** |
| `Ctrl+Shift+O` 仓库管理 | 白名单拒绝；Ctrl+O 还会先弹文件框 | P3，需白名单 + guard |
| 组合键自定义 | 只支持单键 | P4，最大 |
| `Shift+Enter` 新窗口 | 无通用 spawn helper | P2/P4，先定义语义 |
| 仓库切换保留标签 | 状态层面成立，但没有文件页直达入口 | 依赖 P1/P3 |
| 重新定位、设为默认 | 不存在 | P3 |
| 文件夹选择框错误码 | 不存在 | P3 小项 |
| 非 md 灰色 | 表述不准确 | 改成“不支持的格式” |

## 4. 方案需要修正的地方

### 4.1 首页固定标签：先定义“唯一首页”还是“任意空标签”

当前“空标签 = 启动页”是全局约定。两种做法：

- **A. 唯一首页**：给 `DocTab` 加 `bool home`（或 role）。启动时确保 index 0 是 home；`homeActive` 改成只认这个标签；点击文件打开新标签；home 不可关闭、不可拖动、不可被“关闭其他/左侧/右侧”关掉；`Ctrl+T` 改成激活首页或新建普通空标签（需决策）。
- **B. 任意空标签仍是首页**：保留现状，只是打开文件时不关 shell 标签。实现小，但会出现多个“首页”标签，语义不干净。

建议 A。它才符合“永久最左标签”。

### 4.2 首页固定标签必须同时处理 session

建议启动逻辑改成：

1. 有仓库、无文件参数：先恢复 session 中的文件标签；
2. 在 index 0 插入 home 标签；
3. 默认激活 home（或恢复上次 active，需决策）；
4. 保存 session 时跳过 home（空路径本来就会被跳过）。

否则就会延续现在的 session 清空问题。

### 4.3 单文件启动和 `openInTabs=false`

- 从 Explorer 打开单个文件时，是否也要在左边加 home？如果加，单文件启动会从“无标签栏”变成“两标签栏”，视觉变化明显。
- `openInTabs=false` 是“一个窗口一个文档”的外部启动模式；如果每个窗口都带 home，需要定义 home 是否只属于主窗口。
- 建议：**主窗口**有 home；由文件参数拉起的窗口可以不带 home，或者带但默认激活文件标签。这个要定。

### 4.4 `Ctrl+Shift+E` 的语义

当前：

- `Ctrl+E`：编辑页显示/隐藏预览栏；
- `Ctrl+Shift+E`：进入全宽阅读视图（编辑缓冲的只读覆盖层）；
- 在全宽阅读视图按 Esc：回到编辑器源码；
- 从编辑回阅读页：干净缓冲双击 Esc，脏缓冲保存询问。

方案里写“编辑加预览 → Esc（或 Ctrl+Shift+E）回只预览”是不准确的。要么改方案措辞，要么改键位语义；不要把两者混在一起。

### 4.5 快速跳转：`B` 和 `Ctrl+P` 都不能直接用

- `B` 已经是文件夹浏览器（[input.cpp](../src/input.cpp#L4091-L4109)），而且它是可配置的 `KA_BROWSE`。如果抢过来，文件夹浏览器就没有默认键了。
- `Ctrl+P` 是打印预览，编辑态和阅读态都有。
- `Ctrl+K` 当前没有绑定，且不需要改白名单（只要 Ctrl，不按 Shift）。**建议第一期固定 `Ctrl+K`**；以后扩展组合键自定义时再把它变成可配置动作。
- 如果一定要“在阅读页按 B 打开快速跳转”，建议新增独立动作并保留文件夹浏览器的键，或把文件夹浏览器改成另一个键；不要直接覆盖。

### 4.6 “Enter 遵循 openInTabs”会改变设置语义

当前 `openInTabs` 的文案和行为是“从 Explorer 打开的文件是否并入现有窗口”，不是“应用内打开是否新开窗口”。建议：

- 应用内 Enter 永远开标签（符合现有标签模型和性能）；
- `Shift+Enter` 显式新窗口；
- `openInTabs` 继续只管外部文件启动；
- 如果坚持“Enter 跟随设置”，需要先把设置重命名为“应用内打开方式”，并处理 `openInTabs=false` 时 Home 自己是否也新开窗口。

新窗口需要一个新的 helper：现有 `launchDocumentWindow` 会带 `--edit`，不适合快速跳转的“只读打开”。

### 4.7 首页 `n/r/d`

- 当前 Home 的 `N` 会掉到全局 `KA_NEWFILE`，直接把 Home 标签变成快速笔记（实测标题 `Tinta - Untitled`）。如果要做首页新建，必须在 `homeOnKeyDown` 里先消费 `N`，不能让它继续往下走。
- `r` 重命名、`d` 删除目前完全没有文件系统操作、确认框或回收站逻辑。这不是“加三个快捷键”，而是三个新功能；删除要走 `IFileOperation` 或 `SHFileOperation` 的回收站语义，并处理只读/占用/失败。
- 建议把 `n/r/d` 从 P1 拆成 P1b，不要和“首页固定标签”绑在一个提交里。

### 4.8 Esc 编辑态：单次还是双击

你的规则是“Esc 只关最上面一层”，但当前干净缓冲双击 Esc 是刻意的防误触设计。两种选择：

- **单次 Esc**：符合新规则，但会改变现有习惯；
- **保留双击**：与“全局唯一”不一致，需要把它写成例外。

我建议先按“**脏缓冲询问、干净缓冲单次回预览**”实现，并把双击保护做成设置项或直接去掉；这属于 UX 决策，最好在 P0/P1 时明确。

另外，方案把“禅模式”排在“编辑页”之前，当前编辑态的 Esc 是先走编辑器、根本到不了禅模式。要按方案执行，需要在编辑器之前加一层全局 Esc 路由。

## 5. 建议实施顺序（修订版）

`[提案]` 以下是当时的实施建议；当前采用状态以 [dev-status.md](dev-status.md) 为准。

### P0 · Esc 永不退出（小、独立、先做）

- `homeKey` / picker 对 Esc 始终返回 true；首页无过滤时什么也不做。
- 删除通用 switch 里的 `WM_CLOSE` 兜底。
- 阅读页 Esc 先做“什么也不做”；等 P1 有首页标签后再接“切回首页”。
- 增加集成测试：Home、Picker、阅读页分别按 Esc，进程仍存活；首页有过滤时 Esc 清过滤。
- 增加 `home_logic_tests`：Esc 在首页空过滤时返回 true。

### P1 · 首页固定标签 + session 合并

- 给 `DocTab` 加 home 角色；启动时确保 index 0。
- 合并 session：home + 文件标签；保存时跳过 home。
- 改 `openFromHome`、`startPageInvoke`、最近文件：不关 shell 标签。
- 关闭、拖动、右键菜单、关闭其他/左/右全部保护 home。
- 决定 `Ctrl+T`、单文件启动、`openInTabs=false` 的行为。
- 测试：打开文件后 home 仍在；关闭文件回 home；重启后文件标签 + home 都在。

### P1b · 首页 `n/r/d`

- 在 Home 的键路由里先消费 `N`，再走全局 newfile。
- 新建文件/文件夹复用现有 naming 逻辑或新增 Home 内联命名。
- 重命名/删除走确认框和回收站；失败要有红字提示。

### P2 · 快速跳转 + 全仓库索引

- 新增 `QuickJump` 状态和渲染；列表来源：`app.tabs`、`startPageRecents`、仓库索引。
- 后台递归扫描仓库，只收支持的文档；忽略隐藏目录和 `.git`，结果带 generation。
- 固定 `Ctrl+K`；保留 `B` 给文件夹浏览器。
- Enter 开标签；`Shift+Enter` 新窗口（新 helper）。
- Tab/Backspace 的目录进入/返回需要单独的浏览状态。
- 测试：空仓库、超大仓库、索引未完成时按 Enter、浮窗 Esc 不改状态。

### P3 · 仓库管理

- 选择页增加“设为默认”“重新定位”“启动方式”入口；或做独立管理页。
- `Ctrl+Shift+O`：白名单加 O + `Ctrl+O` 分支加 `!shift` + 编辑器/阅读态统一处理。
- 运行中仓库丢失：自动回选择页或红字提示 + 重新定位。
- 文件夹选择框区分取消和 HRESULT 错误。

### P4 · 组合键自定义

- `KeyBinding` 增加 ctrl/shift 标志；ini 格式、profile、匹配、翻译、捕获 UI、冲突检测、迁移全部要改。
- 这是最大的一项，放最后合理；第一期不要为了快速跳转去动它，固定 `Ctrl+K` 即可。

## 6. `j/k` 结论

**保持现状：`j` 向下、`k` 向上。** 这与 vim/Yazi 一致，Home、选择页、阅读滚动三处也一致。方案文字里的“j(上) k(下)”需要改掉，不需要改代码。

## 7. 需要你确认的点

1. 三张流程图没有附上；如果图里还有状态/按键，请发来再对。
2. 首页固定标签：唯一 home（推荐）还是任意空标签都算首页？
3. 启动时 home 与恢复的 session：默认激活 home，还是恢复上次 active 标签？
4. 单文件启动和 `openInTabs=false` 时，是否也带 home？
5. 编辑页 Esc：单次回预览，还是保留双击保护？
6. 快速跳转用 `Ctrl+K`（推荐）还是坚持 `B` / `Ctrl+P`？
7. `Shift+Enter` 的“新窗口”是显式动作，还是确实要让 Enter 跟随 `openInTabs`？
8. `Home n/r/d` 是否先只做 `n`，把重命名/删除拆到后续？

## 8. 证据索引

- Esc 兜底：[input.cpp](../src/input.cpp#L4037-L4070)
- Home 键路由：[homeview.cpp](../src/homeview.cpp#L567-L590)
- Home 逻辑：[home_logic.cpp](../src/home_logic.cpp#L222-L281)
- Alt/白名单：[keymap.cpp](../src/keymap.cpp#L5-L15)
- KeyBinding：[keymap.h](../include/keymap.h#L11-L14)
- 快捷键捕获：[input.cpp](../src/input.cpp#L3639-L3657)
- 标签模型：[app.h](../include/app.h#L513-L527)
- 打开文件/关闭 shell：[homeview.cpp](../src/homeview.cpp#L67-L73)、[input.cpp](../src/input.cpp#L2531-L2539)
- 最后标签变启动页：[tabs.cpp](../src/tabs.cpp#L243-L251)
- Ctrl+T：[tabs.cpp](../src/tabs.cpp#L313-L335)
- session 保存：[main_d2d.cpp](../src/main_d2d.cpp#L1825-L1838)
- 启动分支：[main_d2d.cpp](../src/main_d2d.cpp#L2227-L2293)
- 编辑 Esc：[editor.cpp](../src/editor.cpp#L2533-L2569)
- `Ctrl+Shift+E`：[editor.cpp](../src/editor.cpp#L2740-L2744)
- 文件夹浏览器 `B`：[input.cpp](../src/input.cpp#L4091-L4109)
- `Ctrl+P`：[input.cpp](../src/input.cpp#L3875-L3881)、[editor.cpp](../src/editor.cpp#L2735-L2739)
- 同级目录搜索：[search.cpp](../src/search.cpp#L124-L174)
- 支持的文件类型：[document.cpp](../src/document.cpp#L103-L123)
- 仓库持久化：[homeview.cpp](../src/homeview.cpp#L98-L127)
- 设置保存错误：[overlays.cpp](../src/overlays.cpp#L3216-L3223)
- 文件夹选择框：[homeview.cpp](../src/homeview.cpp#L177-L202)
