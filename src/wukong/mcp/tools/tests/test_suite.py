"""Wukong MCP tools test suite — declarative C test registry.

Tests for code in wukong/mcp/tools/ (mcp_tool_tm.c, etc.).

Usage:
    pytest test_suite.py -v               # all tests, verbose
    pytest test_suite.py -k mcp_tools     # single test
    pytest test_suite.py -v --tb=short    # compact failures
"""

import pytest

# (id, description, sdk_sources, common_sources, test_sources, extra_sdk_includes)
_TESTS = [
    ("mcp",
     "MCP tool handlers for alarm/schedule/countdown",
     ["wukong/mcp/tools/mcp_tool_tm.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp.c", "test_mcp.c"],
     ["wukong/mcp/tools", "wukong/tm"]),
    ("mcp_tools",
     "MCP tool schema + integration (ex test_wukong_mcp_tm_tools.sh)",
     ["wukong/mcp/tools/mcp_tool_tm.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_tools.c", "test_mcp_tools.c"],
     ["wukong/mcp/tools", "wukong/tm"]),
]

_IDS = [t[0] for t in _TESTS]


@pytest.mark.parametrize(
    "name,description,sdk_sources,common_sources,test_sources,extra_includes",
    _TESTS,
    ids=_IDS,
)
def test_wukong_mcp_tools(
    name, description, sdk_sources, common_sources, test_sources,
    extra_includes, c_test,
):
    """Compile and run a wukong/mcp/tools C unit test with TAP output."""
    for src in test_sources:
        c_test.add_test_source(src)
    for src in common_sources:
        c_test.add_common_source(src)
    for src in sdk_sources:
        c_test.add_sdk_source(src)
    for inc in extra_includes:
        c_test.include_sdk(inc)
    c_test.run()
