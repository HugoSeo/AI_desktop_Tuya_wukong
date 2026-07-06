# tuyaos_demo_wukong_ai 开发者文档体系结构设计

本文档定义 **tuyaos_demo_wukong_ai** 应用的开发者文档层级、目录约定与内容规范，遵循 `.cursor/rules/tuyaos-doc-gen.mdc`。

---

## 一、总体原则

| 原则 | 说明 |
|------|------|
| **双语文档** | 每个模块目录下同时维护 **README_CN.md**（中文）与 **README.md**（英文），结构一一对应，仅正文语言不同；代码、类型名、宏、路径保持英文。 |
| **七章结构** | 模块级文档必含：标题 → 概述 → 目录结构 → 处理流程 → API 参考 → 使用示例 → 支持。 |
| **就近放置** | 文档放在对应模块目录内，便于与代码同步维护。 |
| **向上引用** | 父级文档通过「目录结构」或「子模块索引」链接到子目录的 README；子目录有 README_CN.md + README.md 则直接链接，无则阅读代码写 1～3 句概要。 |

---

## 二、文档层级与路径

```
apps/tuyaos_demo_wukong_ai/
├── README_CN.md                    # [L0] 框架说明（中文）= 项目说明 + 源码与文档结构 + 索引
├── README.md                       # [L0] 框架说明（英文）= 项目说明 + 源码与文档结构 + 索引
├── docs/                           # 文档与规范
│   ├── DOC_STRUCTURE.md            # 本文档：文档体系结构设计
│   ├── QUICKSTART_CN.md            # 快速开始（中文）
│   └── QUICKSTART.md               # 快速开始（英文）
│
└── src/                            # 应用源码根（无单独 L1 文档，说明已并入 L0）
    ├── wukong/                      # [L2] Wukong AI 核心域
    │   ├── README_CN.md
    │   ├── README.md
    │   ├── audio/                   # [L3] 模块
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   ├── kws/
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   ├── mode/                   # 若与 src/mode 合并则此处为链接或说明
    │   ├── skills/
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   ├── mcp/
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   ├── picture/
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   ├── assets/
    │   │   ├── README_CN.md
    │   │   └── README.md
    │   └── (video/ 等按需补充)
    │
    ├── mode/                        # [L2] 对话模式域（与 wukong 协作）
    │   ├── README_CN.md
    │   └── README.md
    │
    ├── boards/                      # [L2] 板级与产品形态
    │   ├── README_CN.md
    │   ├── README.md
    │   └── T5AI_BOARD_*/            # 各板级子目录按需 README_CN.md + README.md
    │
    ├── miscs/                       # [L2] 通用能力（GUI、音频、编解码等）
    │   ├── README_CN.md             # miscs 总览
    │   ├── README.md
    │   ├── gui/                     # 可只做总览或对关键子模块单独 README
    │   ├── audio_player/
    │   ├── uart_codec/
    │   └── ...
    │
    └── drivers/                     # [L2] 应用层驱动
        ├── README_CN.md             # drivers 总览
        ├── README.md
        └── app_tuya_*/              # 各驱动子目录按需 README_CN.md + README.md
```

- **L0 框架说明**：整个项目的说明性入口（根目录 README_CN.md / README.md），包含项目概述、快速开始链接、**源码与文档结构**（五域树 + 各域/模块简要说明 + 到对应 README 的链接，即源码总览与文档索引融合）、支持。原 L1（src 引子）已合并至 L0，不再单独维护 src/README。
- **L2**：域级（如 wukong、mode、boards、miscs、drivers），概述 + 目录结构 + 指向 L3。
- **L3**：具体模块（如 wukong/audio、wukong/kws），严格按规范七章书写，且双语文档一致。

---

## 三、各层级内容要求

### 3.0 快速开始模块（docs/QUICKSTART_CN.md / docs/QUICKSTART.md）

独立成篇，供新开发者从零到跑通应用。中英文各一份，结构一致。

| 章节 | 中文标题 | 英文标题 | 内容建议 |
|------|----------|----------|----------|
| 1 | 前置条件 | Prerequisites | 开发环境（OS、工具链、SDK 版本）、账号/密钥等 |
| 2 | 获取源码 | Get the Source | 仓库克隆、子模块、依赖拉取（含 prepare 等） |
| 3 | 配置与编译 | Configure and Build | 选择板型/平台、menuconfig、编译命令与产物路径 |
| 4 | 烧录与运行 | Flash and Run | 烧录方式、首次上电、基础功能自检 |
| 5 | 下一步 | Next Steps | 指向框架说明（根 README）、各域/模块文档或配置说明 |

- 以步骤化、可复制命令为主；涉及路径、板型名、端口等用占位或示例说明。
- 框架说明中的「快速开始」段落应**链接到** `[快速开始](docs/QUICKSTART_CN.md)` / `[Quick Start](docs/QUICKSTART.md)`，避免在根 README 中重复长文。

### 3.1 L0：框架说明（根目录 README_CN.md / README.md）

作为**整个项目的说明性入口**，将项目说明、源码总览与文档索引合并为一篇：既说明项目是什么、如何快速开始，又用同一棵「源码与文档结构」树展示目录与文档链接。

| 章节 | 内容建议 |
|------|----------|
| 标题 | 项目/应用名称 + 一句话说明（中/英） |
| 概述 | 项目定位、目标设备/场景、主要能力、整体架构或模块划分（1～3 段） |
| 快速开始 | 简要 1～2 句 + **链接到** `[快速开始](docs/QUICKSTART_CN.md)` / `[Quick Start](docs/QUICKSTART.md)`，不在此展开编译/烧录细节 |
| 源码与文档结构 | **融合**源码总览与文档索引：按层级列出 `src/` 下五域（wukong、mode、boards、miscs、drivers）及下属模块，每项为「简要说明 + 到对应 README 的链接」（如 `[Wukong 核心](src/wukong/README_CN.md)`、`[KWS 模块](src/wukong/kws/README_CN.md)`）；可先列「快速开始」链接，再展开五域树。读者从本页即可理解源码布局并跳转到任意文档。中文版链接到各 `README_CN.md`，英文版链接到各 `README.md`。可选：在域级下附一句应用主流程或域间关系。 |
| 支持 | 固定支持段落（论坛链接，同规范） |

可不完全按「七章」展开，以说明性入口 + 源码与文档结构为主。（L1 src 引子已取消，不再单独维护 src/README。）

### 3.2 L2：域级（如 wukong/、mode/、boards/、miscs/、drivers/）

| 章节 | 内容建议 |
|------|----------|
| 概述 | 该域的职责与在应用中的角色 |
| 目录结构 | 树形列出子目录；有 README_CN.md+README.md 的用 `[子目录名](子目录/README_CN.md)` 形式链接并简短说明，无则阅读代码写 1～3 句 |
| 处理流程 | 域内主流程或与其它域的协作关系（ASCII 图） |
| （可选）API 参考 | 若域有统一对外头文件，可简述或链接到子模块 API |
| 支持 | 固定支持段落 |

### 3.3 L3：模块级（如 wukong/audio、wukong/kws、wukong/mcp）

**必须** 符合 `tuyaos-doc-gen.mdc` 的七章结构：

| 顺序 | 中文标题 | 英文标题 | 说明 |
|------|----------|----------|------|
| 1 | 标题 | Title | `# 模块名（中文说明）》` / `# Module Name` |
| 2 | 概述 | Overview | 1～3 句话，职责与接入方式 |
| 3 | 目录结构 | Directory Structure | 树形 + 子目录链接或代码摘要 |
| 4 | 处理流程 | Processing Flow | ASCII 流程图，多模式用 `### 模式一：xxx (MACRO=1)` |
| 5 | API 参考 | API Reference | 仅公共 API，含签名、参数、返回值、注意/警告 |
| 6 | 使用示例 | Usage | 典型调用与配置示例 |
| 7 | 支持 | Support | 固定论坛链接（中/英） |

---

## 四、现有文档与待补齐

| 路径 | 已有 | 待补齐 | 说明 |
|------|------|--------|------|
| 应用根 | README_CN.md, README.md | — | L0 框架说明（项目说明 + 源码与文档结构 + 索引，与源码总览已合并） |
| docs/ | DOC_STRUCTURE.md, QUICKSTART_CN.md, QUICKSTART.md | — | 快速开始已就绪 |
| src/wukong/ | README_CN.md | README.md | 与子模块链接统一用 README.md 或 README_CN.md 需统一 |
| src/wukong/audio/ | README_CN.md | README.md | 补英文 |
| src/wukong/kws/ | README_CN.md | README.md | 补英文 |
| src/wukong/mcp/ | README.md | README_CN.md | 补中文 |
| src/wukong/assets/ | README_CN.md | README.md | 补英文 |
| src/wukong/skills/ | - | README_CN.md, README.md | 新建 |
| src/wukong/picture/ | - | README_CN.md, README.md | 新建 |
| src/wukong/mode/ 或 src/mode | - | 与 src/mode 关系需明确 | 若为同一概念可合并说明 |
| src/mode/ | README_CN.md | README.md | 补英文 |
| src/boards/ | README_CN.md | README.md | 补英文 |
| src/miscs/ | - | README_CN.md, README.md | 总览；子模块按需 |
| src/drivers/ | 部分子目录有 README | 总览 README_CN.md/README.md + 缺的双语 | 按需补全 |

---

## 五、引用与维护约定

1. **子目录引用**  
   - 有 `README_CN.md` 且 `README.md`：在父级「目录结构」中写  
     `├── 子目录名/  # [子目录名](子目录名/README_CN.md) - 一句话概述`  
     英文版链接到 `子目录名/README.md`。
   - 无上述文档：同位置写 1～3 句从代码得出的职责/接口说明，不写链接。

2. **中英文一致**  
   同一模块的 README_CN.md 与 README.md 章节顺序、标题层级、代码与符号完全一致，仅自然语言中/英不同。

3. **支持段落**  
   - 中文：在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。  
   - 英文：If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.

4. **流程图**  
   使用 ASCII 框图（`┌─┐` `│ │` `└─┘` `│` `▼` `►`），步骤可编号（如 `1. 初始化`），多模式用 `### 模式一：xxx (MACRO=1)` 区分。

5. **API 与注意事项**  
   仅文档化头文件中的公共 API；对非常规行为、调用顺序、线程/中断约束、内存归属等用 `> **注意**` 或 `> **警告**` 明确标出。

---

## 六、实施顺序建议

1. **快速开始**：在 `docs/` 下新增 `QUICKSTART_CN.md`、`QUICKSTART.md`，按 3.0 节结构填写前置条件、获取源码、配置编译、烧录运行与下一步链接。  
2. **L0 框架说明**：在应用根目录新增 `README_CN.md`、`README.md`，作为项目说明性入口；包含概述、快速开始（链接到 `docs/QUICKSTART_CN.md` / `docs/QUICKSTART.md`）、**源码与文档结构**（五域树 + 各域/模块简要说明 + 到对应 README 的链接，融合源码总览与文档索引）、支持。（L1 src 引子已取消。）  
3. **L2**：为 wukong、mode、boards、miscs、drivers 补齐或统一双语域级 README。  
4. **L3**：对 wukong 下 audio、kws、mcp、skills、picture、assets 等按「七章」补全或新建双语模块文档；mode、boards、drivers 下关键子目录按需补全。  
5. **交叉检查**：确保框架说明中「源码与文档结构」的链接与各域/模块路径一致，且中英文成对存在；快速开始链接正确。

按上述结构实施后，即可形成以「框架说明（含源码总览与索引）→ 域 → 模块」为层级的开发者文档体系，并与 `tuyaos-doc-gen.mdc` 保持一致。
