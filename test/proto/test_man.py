"""MAN — Manual centering and movement (CONTROL manual rates)."""
import time

import pytest

from client import DeviceError
from conftest import axis_position, wait_not_moving, wait_slewing


@pytest.fixture(autouse=True)
def _stop(proto):
    yield
    proto.control(manual_ra_dps=0.0, manual_dec_dps=0.0)


def test_move_axis_constant_speed(proto):
    """manual_ra_dps moves RA at constant speed."""
    proto.control(manual_ra_dps=1.0)
    wait_slewing(proto)
    proto.control(manual_ra_dps=0.0)
    wait_not_moving(proto)
    assert proto.state()["state"] != "slewing"


def test_rejected_rate_not_remembered(proto):
    """A manual rate rejected while parked must not leak into a later move.

    Regression: the firmware cached the rate before motors accepted it, so a
    rejected RA order left s_ra_rate set and a later single-axis DEC move
    replayed it (RA=1, DEC=1).  A rejected order must change nothing.
    """
    proto.action("park")
    with pytest.raises(DeviceError):
        proto.control(manual_ra_dps=1.0)
    proto.action("unpark")

    # Request only DEC; the rejected RA rate must not reappear.
    proto.control(manual_dec_dps=1.0)
    wait_slewing(proto)
    st = proto.state()
    assert st["ra_speed_dps"] == 0.0, f"rejected RA rate leaked: {st['ra_speed_dps']}"
    assert st["dec_speed_dps"] != 0.0
    proto.control(manual_dec_dps=0.0)
    wait_not_moving(proto)


def test_move_axis_reverse(proto):
    """Reversing the direction reverses the RA motion."""
    proto.control(manual_ra_dps=1.0)
    wait_slewing(proto)
    time.sleep(0.3)
    before = axis_position(proto)["ra_deg"]

    proto.control(manual_ra_dps=-1.0)
    time.sleep(1.0)
    after = axis_position(proto)["ra_deg"]

    assert after < before, f"RA did not reverse: before={before:.3f}° after={after:.3f}°"
    proto.control(manual_ra_dps=0.0)
    wait_not_moving(proto)


def test_move_two_axes_stop_one(proto):
    """Stopping one axis preserves the other's motion."""
    proto.control(manual_ra_dps=1.0, manual_dec_dps=1.0)
    wait_slewing(proto)
    proto.control(manual_ra_dps=0.0)   # stop only RA; DEC keeps going
    time.sleep(0.3)
    dec0 = axis_position(proto)["dec_deg"]
    time.sleep(0.6)
    dec1 = axis_position(proto)["dec_deg"]
    assert abs(dec1 - dec0) > 0.1, f"DEC stopped when RA was stopped (Δ={dec1 - dec0:.3f}°)"
    proto.control(manual_dec_dps=0.0)
    wait_not_moving(proto)


def test_release_axis_repeatedly(proto):
    """Releasing the control repeatedly is idempotent."""
    proto.control(manual_ra_dps=1.0)
    wait_slewing(proto)
    for _ in range(3):
        proto.control(manual_ra_dps=0.0)
    assert proto.state()["state"] != "slewing"


def test_change_rate_without_release(proto):
    """Change the speed without releasing the control."""
    proto.control(manual_ra_dps=1.0)
    wait_slewing(proto)
    proto.control(manual_ra_dps=3.0)
    time.sleep(0.3)
    assert proto.state()["state"] == "slewing"
    proto.control(manual_ra_dps=0.0)
    wait_not_moving(proto)


@pytest.mark.parametrize("axis,limit", [(0, 100.0), (1, 150.0)])
def test_move_axis_limit(proto, axis, limit):
    """MoveAxis stops at the axis limit, never passes it, and moves back."""
    proto.action("home")
    wait_not_moving(proto)

    rate = {"ra": 6.0, "dec": 6.0}
    if axis == 0:
        proto.control(manual_ra_dps=rate["ra"])
    else:
        proto.control(manual_dec_dps=rate["dec"])
    wait_not_moving(proto, timeout=40)
    pos = axis_position(proto)
    val = pos["ra_deg"] if axis == 0 else pos["dec_deg"]
    assert val <= limit + 0.1, f"axis {axis} passed the limit: {val}"

    # away from the limit
    if axis == 0:
        proto.control(manual_ra_dps=-6.0)
    else:
        proto.control(manual_dec_dps=-6.0)
    wait_slewing(proto)
    if axis == 0:
        proto.control(manual_ra_dps=0.0)
    else:
        proto.control(manual_dec_dps=0.0)
    wait_not_moving(proto)
    pos2 = axis_position(proto)
    val2 = pos2["ra_deg"] if axis == 0 else pos2["dec_deg"]
    assert val2 < limit, f"axis {axis} did not move away from the limit"
