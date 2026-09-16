#!/usr/bin/env python3
"""wukong 文档检查：死链 / 中英镜像对应 / 路径引用抽检。

用法：在 wukong 仓库根目录运行  python3 scripts/docs_check.py
退出码：0 = 通过（可有警告）；1 = 存在错误。
检查范围：docs/**/*.md + 根 README*.md + build/README_CN.md + src/ 下本仓库自有 markdown
（子模块与 vendor/lvgl 三方目录除外）。
子模块：指向 git 子模块（.gitmodules）内部的目标，在子模块未检出时降级为警告，
不产生假阳性错误；完整校验请先 git submodule update --init --recursive。
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")
CODE_PATH_RE = re.compile(r"`((?:src|docs|scripts)/[A-Za-z0-9_\-./一-鿿]+)`")
# 示例占位路径（ui_page_xxx.c、T5AI_BOARD_NEW、wukong_ai_mode_my_mode.c 等）不校验存在性。
# 注意 _ 属 \w，占位 token 常以 _ 相邻（ui_page_xxx），\b 在此失效，故用显式分隔符边界。
PLACEHOLDER_RE = re.compile(r"(?:^|[\W_])xxx(?:[\W_]|$)|_new\b|(?:^|[\W_])my_|<[^>]*>", re.I)

def _submodule_dirs() -> list:
    gm = REPO / ".gitmodules"
    if not gm.exists():
        return []
    return [(REPO / p).resolve() for p in
            re.findall(r"^\s*path\s*=\s*(\S+)", gm.read_text(encoding="utf-8"), re.M)]

SUBMODULES = _submodule_dirs()

def _app_src_docs() -> list:
    """src/ 下属于本仓库（非子模块、非三方 vendor/lvgl）的 markdown。"""
    out = []
    for p in sorted(REPO.glob("src/**/*.md")):
        rp = p.resolve()
        if any(sm == rp or sm in rp.parents for sm in SUBMODULES):
            continue
        if "lvgl" in p.parts or "vendor" in p.parts:
            continue
        out.append(p)
    return out

SCOPE = (sorted(REPO.glob("docs/**/*.md"))
         + [REPO / "README.md", REPO / "README_CN.md",
            REPO / "CHANGELOG.md", REPO / "CHANGELOG_CN.md"]
         + _app_src_docs() + [REPO / "build" / "README_CN.md"])

def _in_unchecked_submodule(target: Path) -> bool:
    """target 位于某个未检出（目录缺失或为空）的子模块内。"""
    for sm in SUBMODULES:
        if sm == target or sm in target.parents:
            return not sm.exists() or not any(sm.iterdir())
    return False

errors, warnings = [], []

def check_links(md: Path) -> None:
    text = md.read_text(encoding="utf-8")
    for target in LINK_RE.findall(text):
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        path = target.split("#", 1)[0]
        if not path:
            continue
        resolved = (md.parent / path).resolve()
        if not resolved.exists():
            if _in_unchecked_submodule(resolved):
                warnings.append(f"{md.relative_to(REPO)}: 目标在未检出子模块内，跳过校验 -> {target}")
            else:
                errors.append(f"{md.relative_to(REPO)}: 死链 -> {target}")
    for ref in CODE_PATH_RE.findall(text):
        if PLACEHOLDER_RE.search(ref):
            continue
        # `foo.h/.c` 合并写法：任一实际存在即可
        m = re.fullmatch(r"(.+)\.h/\.c", ref)
        if m and ((REPO / f"{m.group(1)}.h").exists() or (REPO / f"{m.group(1)}.c").exists()):
            continue
        if not (REPO / ref.rstrip("/")).exists():
            warnings.append(f"{md.relative_to(REPO)}: 引用路径不存在 -> `{ref}`")

def check_mirror() -> None:
    # images/ 下只有资源说明，不要求英文镜像
    cn = {p.relative_to(REPO / "docs") for p in (REPO / "docs").glob("**/*.md")
          if not {"en", "images"} & set(p.relative_to(REPO / "docs").parts)}
    en_dir = REPO / "docs" / "en"
    en = {p.relative_to(en_dir) for p in en_dir.glob("**/*.md")} if en_dir.exists() else set()
    for rel in sorted(cn - en):
        errors.append(f"docs/{rel}: 缺英文镜像 docs/en/{rel}")
    for rel in sorted(en - cn):
        errors.append(f"docs/en/{rel}: 无对应中文源 docs/{rel}")
    # 根目录 changelog 与根 README 同为中英成对（不在 docs/ 镜像体系内，单独校验）
    if (REPO / "CHANGELOG_CN.md").exists() and not (REPO / "CHANGELOG.md").exists():
        errors.append("CHANGELOG_CN.md: 缺英文镜像 CHANGELOG.md")
    if (REPO / "CHANGELOG.md").exists() and not (REPO / "CHANGELOG_CN.md").exists():
        errors.append("CHANGELOG.md: 无对应中文源 CHANGELOG_CN.md")

for f in SCOPE:
    if f.exists():
        check_links(f)
check_mirror()

for w in warnings:
    print(f"WARN  {w}")
for e in errors:
    print(f"ERROR {e}")
print(f"\ndocs_check: {len(errors)} error(s), {len(warnings)} warning(s)")
sys.exit(1 if errors else 0)
