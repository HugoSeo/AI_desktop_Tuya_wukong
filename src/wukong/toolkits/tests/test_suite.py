"""wukong_tool base test suite (conftest auto-discovered).

Exercises the neutral tool base (wukong/toolkits/wukong_tool.c) directly:
exec result shaping per caller, flags-gated visibility, JSON-encoded-string
argument promotion, and the business-error (rc != OK + text) contract.

Usage:
    pytest test_suite.py -v
"""

import pytest

# (id, description, sdk_sources, common_sources, test_sources, extra_sdk_includes)
_TESTS = [
    ("wukong_tool",
     "tool base: exec shaping, flags visibility, arg promotion, business error",
     ["wukong/toolkits/wukong_tool.c"],
     ["stubs_cjson.c"],
     ["test_wukong_tools.c"],
     ["wukong/toolkits", "wukong/provider"]),
]

_IDS = [t[0] for t in _TESTS]


@pytest.mark.parametrize(
    "name,description,sdk_sources,common_sources,test_sources,extra_includes",
    _TESTS,
    ids=_IDS,
)
def test_wukong_tool_base(
    name, description, sdk_sources, common_sources, test_sources,
    extra_includes, c_test,
):
    """Compile and run the wukong_tool base C unit test with TAP output."""
    for src in test_sources:
        c_test.add_test_source(src)
    for src in common_sources:
        c_test.add_common_source(src)
    for src in sdk_sources:
        c_test.add_sdk_source(src)
    for inc in extra_includes:
        c_test.include_sdk(inc)
    c_test.run()
