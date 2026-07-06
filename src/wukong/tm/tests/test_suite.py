"""Wukong TM test suite — declarative C test registry.

Tests for code in wukong/tm/. MCP tool tests live in wukong/mcp/tools/tests/.

Usage:
    pytest test_suite.py -v               # all tests, verbose
    pytest test_suite.py -k stopwatch     # single test
    pytest test_suite.py -v --tb=short    # compact failures
"""

import pytest

# (id, description, sdk_sources, common_sources, test_sources, extra_sdk_includes)
_TESTS = [
    ("core",
     "Core init/deinit and time-sync event",
     ["wukong/tm/wukong_tm.c"],
     [],
     ["stubs_core.c", "test_core.c"],
     []),
    ("stopwatch",
     "Stopwatch start/pause/resume/stop/reset operations",
     ["wukong/tm/wukong_tm.c", "wukong/tm/wukong_tm_stopwatch.c"],
     [],
     ["stubs_stopwatch.c", "test_stopwatch.c"],
     []),
    ("alarm",
     "Alarm add/update/delete/fire/ack/snooze/remove-by-time",
     ["wukong/tm/wukong_tm.c", "wukong/tm/wukong_tm_alarm.c"],
     ["stubs_cjson.c"],
     ["stubs_alarm.c", "test_alarm.c"],
     []),
    ("countdown",
     "Countdown create/pause/resume/delete and cron scheduling",
     ["wukong/tm/wukong_tm.c", "wukong/tm/wukong_tm_countdown.c"],
     ["stubs_cjson.c"],
     ["stubs_countdown.c", "test_countdown.c"],
     []),
    ("pomodoro",
     "Pomodoro start/pause/resume/stop and phase transitions",
     ["wukong/tm/wukong_tm_pomodoro.c"],
     ["stubs_cjson.c"],
     ["stubs_pomodoro.c", "test_pomodoro.c"],
     []),
    ("reminder",
     "Reminder add/fire/remove-by-time and cron integration",
     ["wukong/tm/wukong_tm.c", "wukong/tm/wukong_tm_alarm.c",
      "wukong/tm/wukong_tm_reminder.c"],
     ["stubs_cjson.c"],
     ["stubs_reminder.c", "test_reminder.c"],
     []),
]

_IDS = [t[0] for t in _TESTS]


@pytest.mark.parametrize(
    "name,description,sdk_sources,common_sources,test_sources,extra_includes",
    _TESTS,
    ids=_IDS,
)
def test_wukong_tm(
    name, description, sdk_sources, common_sources, test_sources,
    extra_includes, c_test,
):
    """Compile and run a wukong_tm C unit test with TAP output."""
    for src in test_sources:
        c_test.add_test_source(src)
    for src in common_sources:
        c_test.add_common_source(src)
    for src in sdk_sources:
        c_test.add_sdk_source(src)
    for inc in extra_includes:
        c_test.include_sdk(inc)
    c_test.run()
