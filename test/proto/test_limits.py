"""LIMITS — a runtime-changed limit is enforced (set and driven via the protocol)."""
import time

import pytest

from conftest import axis_position, wait_not_moving


@pytest.fixture(autouse=True)
def _clear_limits(proto):
    yield
    try:
        proto.action("limits", param="clear_limits")
    except Exception:
        pass


def test_changed_dec_limit_respected(proto):
    """After setting a smaller DEC limit, MoveAxis stops there (not +150°)."""
    proto.action("home")
    wait_not_moving(proto)

    # move to ~+6° and set it as the new DEC right limit
    proto.control(manual_dec_dps=6.0)
    time.sleep(1.0)
    proto.control(manual_dec_dps=0.0)
    wait_not_moving(proto)
    proto.action("limits", param="set_dec_right")
    limit = axis_position(proto)["dec_deg"]

    # move back toward 0
    proto.control(manual_dec_dps=-6.0)
    time.sleep(1.0)
    proto.control(manual_dec_dps=0.0)
    wait_not_moving(proto)

    # move positive must stop at the new limit, not pass it
    proto.control(manual_dec_dps=6.0)
    wait_not_moving(proto, timeout=10)
    pos = axis_position(proto)["dec_deg"]
    assert pos <= limit + 0.5, f"DEC passed the changed limit: {pos} > {limit}"
