# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

"""
Validate runtime log level filtering on cpuapp and on each remote shell (radio, ppr, flpr).

Uses the sample ``ping`` command, which emits one log line per severity (err/wrn/inf/dbg).
"""

from __future__ import annotations

import logging

import pytest
from twister_harness import Shell

logger = logging.getLogger(__name__)

# Substrings from LOG_* calls in src/main.c and remote/src/main.c
MARK_ERR = "error 100"
MARK_WRN = "warning"
MARK_INF = "info test"
MARK_DBG = "debug 1000"


def _combined(lines: list[str]) -> str:
    return "\n".join(lines)


def _assert_ping_log_levels(text: str, expect_dbg: bool) -> None:
    assert MARK_ERR in text, f"expected ERR log ({MARK_ERR!r}), output:\n{text[:1200]}"
    assert MARK_WRN in text, f"expected WRN log ({MARK_WRN!r}), output:\n{text[:1200]}"
    assert MARK_INF in text, f"expected INF log ({MARK_INF!r}), output:\n{text[:1200]}"
    if expect_dbg:
        assert MARK_DBG in text, f"expected DBG log ({MARK_DBG!r}), output:\n{text[:1200]}"
    else:
        assert MARK_DBG not in text, f"DBG log should be filtered out, output:\n{text[:1200]}"


@pytest.mark.parametrize(
    "cmd_prefix",
    [
        "",
        "remote_shell radio ",
        "remote_shell ppr ",
        "remote_shell flpr ",
    ],
)
def test_runtime_log_filter_ping(shell: Shell, cmd_prefix: str) -> None:
    """
    For each core (local or via remote_shell): set module ``app`` to DBG, run ``ping`` and
    expect all severities; set to INF, run ``ping`` again and expect no DBG line.
    """
    label = "cpuapp" if not cmd_prefix else cmd_prefix.strip()
    logger.info("testing core %s", label)

    assert shell.wait_for_prompt(timeout=90), "shell prompt not ready"

    log_dbg = f"{cmd_prefix}log enable dbg app".rstrip()
    log_inf = f"{cmd_prefix}log enable inf app".rstrip()
    ping_cmd = f"{cmd_prefix}ping".rstrip()

    logger.debug("send %r", log_dbg)
    out_dbg = shell.exec_command(log_dbg, timeout=60)
    logger.debug("log dbg cmd output: %s", _combined(out_dbg)[-400:])

    logger.debug("send %r", ping_cmd)
    out_ping_all = shell.exec_command(
        ping_cmd,
        timeout=60,
        get_full_output=True,
        full_output_timeout=2.0,
    )
    text_all = _combined(out_ping_all)
    logger.debug("ping (dbg filter) tail: %s", text_all[-800:])
    _assert_ping_log_levels(text_all, expect_dbg=True)

    logger.debug("send %r", log_inf)
    out_inf = shell.exec_command(log_inf, timeout=60)
    logger.debug("log inf cmd output: %s", _combined(out_inf)[-400:])

    logger.debug("send %r again", ping_cmd)
    out_ping_inf = shell.exec_command(
        ping_cmd,
        timeout=60,
        get_full_output=True,
        full_output_timeout=5.0,
    )
    text_inf = _combined(out_ping_inf)
    logger.debug("ping (inf filter) tail: %s", text_inf[-800:])
    _assert_ping_log_levels(text_inf, expect_dbg=False)
