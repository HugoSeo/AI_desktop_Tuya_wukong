"""Shared C unit-test harness for wukong/* (TAP output parsing).

Auto-discovered by pytest for every tests/ subdirectory under wukong/.
SUT location is inferred from the test file path: parent of tests dir
(e.g. wukong/tm/tests/* tests wukong/tm/*; wukong/mcp/tools/tests/*
tests wukong/mcp/tools/*).
"""

import re
import subprocess
from pathlib import Path

import pytest

_WUKONG_DIR = Path(__file__).parent
_APP_SRC = _WUKONG_DIR.parent  # apps/.../src/
_COMMON_DIR = _WUKONG_DIR / "tests_common"


def _parse_tap(stdout: str) -> tuple[int, int]:
    """Parse TAP output, return (passed, total)."""
    passed = total = 0
    for line in stdout.splitlines():
        if re.match(r"^ok \d+", line):
            passed += 1
            total += 1
        elif re.match(r"^not ok \d+", line):
            total += 1
    return passed, total


class CTestHarness:
    """Compile and run C unit tests from real .c files."""

    _CC_FLAGS = ["-D_GNU_SOURCE", "-std=c99", "-Wall", "-Wextra", "-Werror"]

    def __init__(self, tmp_path: Path, tests_dir: Path) -> None:
        self._tmp = tmp_path
        self._tests_dir = tests_dir
        self._sut_dir = tests_dir.parent  # parent of tests/ = SUT dir
        self._sources: list[Path] = []
        self._includes: list[Path] = [
            _COMMON_DIR / "stubs",
            _COMMON_DIR,
            tests_dir,
            self._sut_dir,
        ]

    def add_test_source(self, name: str) -> "CTestHarness":
        """Add a .c source from the local tests/ directory."""
        self._sources.append(self._tests_dir / name)
        return self

    def add_common_source(self, name: str) -> "CTestHarness":
        """Add a .c source from wukong/tests_common/."""
        self._sources.append(_COMMON_DIR / name)
        return self

    def add_sdk_source(self, rel_path: str) -> "CTestHarness":
        """Add a .c source by path relative to apps/.../src/."""
        self._sources.append(_APP_SRC / rel_path)
        return self

    def include_sdk(self, rel_path: str) -> "CTestHarness":
        """Add an include path relative to apps/.../src/."""
        self._includes.append(_APP_SRC / rel_path)
        return self

    def run(self) -> None:
        """Compile, execute, parse TAP; fail on any error."""
        binary = self._tmp / "test_binary"
        cmd = [
            "cc",
            *self._CC_FLAGS,
            *[f"-I{d}" for d in self._includes],
            *[str(s) for s in self._sources],
            "-o", str(binary),
        ]
        comp = subprocess.run(cmd, capture_output=True, text=True)
        if comp.returncode != 0:
            pytest.fail(f"Compilation failed:\n{comp.stderr.strip()}")

        result = subprocess.run(
            [str(binary)], capture_output=True, text=True, timeout=30,
        )
        passed, total = _parse_tap(result.stdout)

        if result.returncode != 0 or passed < total:
            output = result.stdout.strip()
            if result.stderr.strip():
                output += "\n" + result.stderr.strip()
            pytest.fail(f"({passed}/{total} passed)\n\n{output}")


@pytest.fixture
def c_test(tmp_path: Path, request: pytest.FixtureRequest) -> CTestHarness:
    """Provide a CTestHarness; tests dir is the directory of the requesting test."""
    tests_dir = Path(request.fspath).parent
    return CTestHarness(tmp_path, tests_dir)
