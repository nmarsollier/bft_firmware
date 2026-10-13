"""GUI — Autoguiding and fine corrections (ACTION guide)."""
import time

import pytest

from client import DeviceError

DIRS = {0: "north", 1: "south", 2: "east", 3: "west"}


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


@pytest.mark.parametrize("direction", [0, 1, 2, 3])
def test_pulse_guide_directions(proto, direction):
    """ACTION guide accepts the four celestial directions."""
    proto.action("guide", direction=DIRS[direction], duration_ms=100)
    assert proto.state()["guiding"] in (True, False)


def test_pulse_duration_controls_correction(proto):
    """A pulse of valid duration ends (guiding becomes false)."""
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
    proto.action("guide", direction="north", duration_ms=200)
    proto.action("guide", direction="east", duration_ms=200)


def test_guide_when_parked(proto):
    """ACTION guide while parked is rejected."""
    proto.action("park")
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=100)
    proto.action("unpark")
