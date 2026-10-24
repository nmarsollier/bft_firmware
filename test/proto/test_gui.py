"""GUI — Autoguiding and fine corrections (ACTION guide)."""
import time

import pytest

from client import DeviceError
from conftest import setup_site_time, wait_not_moving

DIRS = {0: "north", 1: "south", 2: "east", 3: "west"}


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def _move_dec(proto, degrees):
    proto.action("move", axis="dec", degrees=degrees, speed=4)
    wait_not_moving(proto)


def test_pulse_guide_east_west(proto):
    """ACTION guide accepts the RA (east/west) directions anywhere, including
    at the pole (a mechanical rotation, no celestial N/S ambiguity)."""
    proto.action("guide", direction="east", duration_ms=100)
    proto.action("guide", direction="west", duration_ms=100)
    assert proto.state()["guiding"] in (True, False)


def test_pulse_guide_north_south(proto):
    """ACTION guide accepts N/S away from the pole."""
    _move_dec(proto, 30.0)
    proto.action("guide", direction="north", duration_ms=100)
    proto.action("guide", direction="south", duration_ms=100)
    assert proto.state()["guiding"] in (True, False)


def test_pulse_duration_controls_correction(proto):
    """A pulse of valid duration ends (guiding becomes false)."""
    _move_dec(proto, 30.0)
    proto.action("guide", direction="north", duration_ms=300)
    time.sleep(0.5)
    assert proto.state()["guiding"] is False


def test_set_guide_rates(proto):
    """Change the guide rates via CONFIG and read them back."""
    proto.config_set(guide_rate_ra=0.001, guide_rate_dec=0.001)
    cfg = proto.config_get()["config"]
    assert cfg["guide_rate_ra"] > 0
    assert cfg["guide_rate_dec"] > 0


def test_guide_ra_and_dec(proto):
    """Guide simultaneously in RA and DEC (both axes accepted)."""
    _move_dec(proto, 30.0)
    proto.action("guide", direction="north", duration_ms=200)
    proto.action("guide", direction="east", duration_ms=200)


def test_guide_when_parked(proto):
    """ACTION guide while parked is rejected."""
    proto.action("park")
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=100)
    proto.action("unpark")


def test_guide_at_pole_rejected(proto):
    """A N/S guide at the celestial pole is rejected without moving; E/W is
    allowed as a mechanical rotation."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)

    proto.config_set(guide_rate_dec=0.5)
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=100)
    with pytest.raises(DeviceError):
        proto.action("guide", direction="south", duration_ms=100)
    proto.action("guide", direction="east", duration_ms=100)
    proto.action("guide", direction="west", duration_ms=100)
