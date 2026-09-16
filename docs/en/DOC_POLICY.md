<!-- Auto-translated from ../DOC_POLICY.md. Do not edit manually. -->

# Documentation Policy (DOC_POLICY)

This page is the writing and maintenance policy for all documentation in tuyaos_demo_wukong_ai, in one page.

## Layout

- `docs/`: task-oriented developer documentation (single entry point [README.md](README.md)) — quickstart, architecture, howto/, troubleshooting/.
- The changelog lives at the repository root (open-source convention): [CHANGELOG_CN.md](../../CHANGELOG_CN.md) (Chinese source) + [CHANGELOG.md](../../CHANGELOG.md) (English mirror), paired the same way as the root READMEs.
- `docs/en/`: English mirror, with a directory structure matching `docs/` one-to-one, **generated entirely by AI translation from the Chinese source — do not edit by hand**; each file starts with the fixed comment `<!-- Auto-translated from <relative path to Chinese source>. Do not edit manually. -->`. If the English wording is off, fix the Chinese source or the translation prompt.
- `src/**/README.md`: module reference, kept alongside the module, a single Chinese copy, with a three-section structure (see below).
- File names use lowercase-hyphenated English; body text is Chinese; design documents (spec/plan) are not checked in.

## Module README Three-Section Structure

1. **Overview**: responsibilities, role within the application, how it's wired in (1–3 paragraphs).
2. **Design Points and Pitfalls**: why it's designed this way; threading/timing/memory-ownership constraints; known pitfalls.
3. **How to Change It**: which files/config to touch to change behavior X, with links to the relevant howto.

**Do not write**: directory trees (use `tree`), item-by-item API signature listings (see the header file), or line-by-line restatements of the code flow.

**Do keep**: the three chapters are a minimum skeleton, not a length cap — behavior comparison tables, parameter/event contract tables, JSON format examples, typical usage code, and flow narratives that build a mental model are all scarce content worth keeping, placed inside the relevant chapter or a clearly-titled reference section. When slimming existing docs, cut only the three "do not write" categories and stale content; before deleting or heavily rewriting well-written prose, list the changes and confirm first.

## Howto Fixed Structure

**Goal → Prerequisites → Steps (copy-pasteable commands/code) → Verification → Common Issues**. Added incrementally, driven by real customer issues; steps that have not been verified on real hardware are not merged.

## Changelog

Root `CHANGELOG_CN.md`: when a feature/fix MR is merged, append one line to the `Unreleased` section (re-create it above the latest version section if absent; pure refactor/documentation MRs may be exempt); at release time, archive it into a `## <version> - <date>` section and remove the `Unreleased` section (it exists only during development), with the version number matching the release tag. The English mirror `CHANGELOG.md` is re-translated whenever the Chinese source changes.

## Updates and Checks

- An MR that changes code updates the affected documentation in the same MR (howto steps, module README, architecture, quickstart) — this is enforced by the wukong MR workflow.
- Run `python3 scripts/docs_check.py` before submitting: dead links and mismatched Chinese/English mirrors are hard errors and must be reduced to zero.

## Support Paragraph (Fixed Text)

Chinese: `在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。`
English: `If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.`
