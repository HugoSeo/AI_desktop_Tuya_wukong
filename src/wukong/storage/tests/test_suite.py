"""Wukong storage test suite — declarative C test registry.

Tests for the record layer in wukong/storage/ (wukong_storage_record.c),
running the REAL implementation: tkl_fs is mapped onto POSIX by
tests_common/stub_tkl_fs.c and the record root is redirected to a host temp
directory through the WUKONG_STORAGE_HOST_TEST seam
(wukong_storage_test_set_root()). Each test supplies its own
wukong_storage_ready() — the medium layer (wukong_storage.c) is firmware-only
and is not compiled here.

Usage:
    pytest test_suite.py -v               # all tests, verbose
    pytest test_suite.py -v --tb=short    # compact failures
"""

import pytest

# (id, description, sdk_sources, common_sources, test_sources,
#  extra_sdk_includes, extra_defines)
_TESTS = [
    ("record",
     "Record layer over a real (host) filesystem: write/read round-trip, "
     "OPRT_NOT_FOUND + out-param clearing, '\\0' past len, overwrite "
     "semantics, idempotent delete, medium-unavailable degradation, atomic "
     "write artifacts, .tmp recovery of an interrupted FATFS fallback, and "
     "delete dropping both names",
     ["wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c"],
     ["test_record.c"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),
    ("music",
     "music/cloud namespace: round-trip through the record layer, the record "
     "landing on disk at <root>/tuyaos/music/cloud/playlist.json (ns with '/' "
     "is one more directory level), and byte-for-byte path compatibility "
     "between the legacy ui_fs path "
     "('/sdcard/tuyaos/music/cloud/playlist.json') and the record-layer path",
     ["wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c"],
     ["test_music.c"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),
    ("list_dirs",
     "wukong_storage_list_dirs: only first-level subdirectories of a "
     "namespace (skill catalog's claw/skills/<id>/ scan), excluding regular "
     "files, with a missing namespace degrading to an empty list",
     ["wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c"],
     ["test_list.c"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),
    ("none",
     "NONE branch (no WUKONG_STORAGE_* macro => WUKONG_STORAGE_ENABLE "
     "undefined): the inert record stubs link without tkl_fs and honour the "
     "degradation contract — write errors, read OPRT_NOT_FOUND with cleared "
     "out-params, delete no-op OK, free NULL-safe",
     ["wukong/storage/wukong_storage_record.c"],
     [],
     ["test_record_none.c"],
     [],
     []),
]

_IDS = [t[0] for t in _TESTS]


@pytest.mark.parametrize(
    "name,description,sdk_sources,common_sources,test_sources,extra_includes,"
    "extra_defines",
    _TESTS,
    ids=_IDS,
)
def test_wukong_storage(
    name, description, sdk_sources, common_sources, test_sources,
    extra_includes, extra_defines, c_test,
):
    """Compile and run a wukong storage C unit test with TAP output."""
    for src in test_sources:
        c_test.add_test_source(src)
    for src in common_sources:
        c_test.add_common_source(src)
    for src in sdk_sources:
        c_test.add_sdk_source(src)
    for inc in extra_includes:
        c_test.include_sdk(inc)
    for macro in extra_defines:
        c_test.define(macro)
    c_test.run()
