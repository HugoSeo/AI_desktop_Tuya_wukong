# 文档约定（DOC_POLICY）

本页是 tuyaos_demo_wukong_ai 全部文档的书写与维护约定，一页读完。

## 布局

- `docs/`：面向任务的开发者文档（唯一入口 [README.md](README.md)）——quickstart、architecture、howto/、troubleshooting/。
- 版本说明在仓库根目录（开源惯例）：[CHANGELOG_CN.md](../CHANGELOG_CN.md)（中文源）+ [CHANGELOG.md](../CHANGELOG.md)（英文镜像），与根 README 的中英成对方式一致。
- `docs/en/`：英文镜像，目录结构与 `docs/` 一一对应，**全部由 AI 从中文翻译生成，禁止手改**；文件头部固定注释 `<!-- Auto-translated from <中文源相对路径>. Do not edit manually. -->`。英文表达有问题时改中文源或翻译提示词。
- `src/**/README.md`：模块参考，就近放置，中文单份，三章结构（见下）。
- 文件名统一小写-连字符英文名；正文中文；设计文档（spec/plan）不入库。

## 模块 README 三章结构

1. **概述**：职责、在应用中的角色、接入方式（1~3 段）。
2. **设计要点与坑**：为什么这样设计；线程/时序/内存归属等约束；已知坑。
3. **改动指南**：要改 X 行为动哪几个文件/配置，链接相关 howto。

**不写**：目录树（看 tree）、逐条 API 签名罗列（看头文件）、逐行代码流程复述。

**应保留**：三章是最低骨架不是长度上限——行为对照表、参数/事件契约表、JSON 格式示例、典型用法代码、帮助建立全局认知的流程叙述都属于应保留的稀缺内容，放进对应章节或独立参考小节。瘦身既有文档时只砍上面"不写"的三类与过时内容；删改写得好的成型内容前先列清单确认。

## howto 固定结构

**目标 → 前置条件 → 步骤（可复制命令/代码）→ 验证方法 → 常见问题**。按真实客户问题驱动增补；未经真机验证的步骤不合入。

## 版本说明

根目录 `CHANGELOG_CN.md`：功能/修复 MR 合入时在 `Unreleased` 段追加一行（段不存在时先在最新版本节上方重建；纯重构/文档 MR 可免）；发版时归档为 `## <版本号> - <日期>` 节并移除 `Unreleased` 段（该段仅开发期存在），版本号与发版 tag 一致。英文镜像 `CHANGELOG.md` 随中文源同步重新翻译。

## 更新与检查

- 改代码的 MR 同步更新受影响文档（howto 步骤、模块 README、architecture、quickstart），随同一 MR 提交——流程由 wukong MR 工作流保障。
- 提交前运行 `python3 scripts/docs_check.py`：死链、中英镜像对应为硬错误，须清零。

## 支持段落（固定文案）

中文：`在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。`
英文：`If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.`
