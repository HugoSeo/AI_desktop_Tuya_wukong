"""Wukong MCP tools test suite — declarative C test registry.

Tests for code in wukong/toolkits/tools/ (mcp_tool_tm.c, etc.).

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
     ["wukong/toolkits/tools/tool_tm.c", "wukong/tm/wukong_iso8601.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp.c", "test_mcp.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/tm"]),
    ("mcp_tools",
     "MCP tool schema + integration (ex test_wukong_mcp_tm_tools.sh)",
     ["wukong/toolkits/tools/tool_tm.c", "wukong/tm/wukong_iso8601.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_tools.c", "test_mcp_tools.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/tm"]),
    ("mcp_control",
     "MCP tool handlers for device control (mode set/get)",
     ["wukong/toolkits/tools/tool_control.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_control.c", "test_mcp_control.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider"]),
    ("mcp_system",
     "MCP system tools: time / uptime / memory",
     ["wukong/toolkits/tools/tool_system.c", "wukong/tm/wukong_iso8601.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_system.c", "test_mcp_system.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/tm"]),
    ("mcp_skill",
     "read_skill tool: registration + body passthrough + not-found is_error",
     ["wukong/toolkits/skill/skill_tool.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_skill.c", "test_mcp_skill.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/toolkits/skill"]),
    ("mcp_memory",
     "memory_get/save/update/delete tools: registration + module passthrough",
     ["wukong/provider/claw/tools/memory_tool.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_memory.c", "test_mcp_memory.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/provider/claw/memory", "wukong/provider/claw/tools"]),

    ("mcp_profile",
     "profile_update/reset tools: registration + passthrough + length cap",
     ["wukong/provider/claw/tools/profile_tool.c"],
     ["stubs_cjson.c"],
     ["stubs_mcp_profile.c", "test_mcp_profile.c"],
     ["wukong/toolkits", "wukong/toolkits/tools", "wukong/provider", "wukong/provider/claw/context", "wukong/provider/claw/tools"]),
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
    """Compile and run a wukong/toolkits/tools C unit test with TAP output."""
    for src in test_sources:
        c_test.add_test_source(src)
    for src in common_sources:
        c_test.add_common_source(src)
    for src in sdk_sources:
        c_test.add_sdk_source(src)
    for inc in extra_includes:
        c_test.include_sdk(inc)
    c_test.run()
