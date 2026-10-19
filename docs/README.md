# Tinta 文档索引

原则：**一条规则只在一个文档里定义**，其他文档链接它。文档数量不再增加；新增内容先判断归属。

## 文档职责

| 文档 | 唯一职责 | 不负责 |
| --- | --- | --- |
| [product-brief.md](product-brief.md) | 产品目标、用户任务、V1 范围、非目标 | 状态转换、命令表、模块实现 |
| [design-constitution.md](design-constitution.md) | 不可违背的原则、关键不变量、模块职责边界 | 功能进度、具体交互流程 |
| [interaction-state-machine.md](interaction-state-machine.md) | 界面状态、焦点、命令、状态转换、异常反馈 | 数据归属、索引实现 |
| [input-workspace-model.md](input-workspace-model.md) | 命令路由机制、工作区数据归属、Effect 与索引契约 | 状态表、命令表 |
| [runtime-system-review.md](runtime-system-review.md) | 某次源码现状核查与差距分析；保留证据 | 作为长期规范 |
| [dev-status.md](dev-status.md) | 当前事实、已决定、实现中、已验证、暂缓的事项 | 设计原理 |

## 状态标记

| 标记 | 含义 |
| --- | --- |
| `[现状]` | 代码当前行为，可能是缺陷；不是目标 |
| `[已决定]` | 已经拍板，尚未实现 |
| `[实现中]` | 正在做，尚未验收 |
| `[已验证]` | 代码 + 测试/实测通过 |
| `[提案]` | 尚未拍板，讨论中 |
| `[暂缓]` | 明确以后再做 |
| `[已废弃]` | 不再采用，保留历史 |

## 阅读顺序

1. [product-brief.md](product-brief.md)：我们为什么做、V1 做到哪。
2. [design-constitution.md](design-constitution.md)：哪些原则不能破。
3. [interaction-state-machine.md](interaction-state-machine.md)：用户怎么在状态之间走。
4. [input-workspace-model.md](input-workspace-model.md)：输入怎么路由、数据归谁、索引怎么接。
5. [runtime-system-review.md](runtime-system-review.md)：某次源码核查的快照。
6. [dev-status.md](dev-status.md)：现在到底做了什么、还差什么。

## 维护规则

- 新规则先确定归属，再动笔；不在两个文档里各写一版。
- 代码与文档冲突时，在 `dev-status.md` 记录差异；`runtime-system-review.md` 只记录核查快照，不追着改。
- 状态机、命令表、不变量、数据归属各自只有一个来源。
- 每个改动至少更新 `dev-status.md` 的一行；只有行为/契约变化才改状态机或宪法。
