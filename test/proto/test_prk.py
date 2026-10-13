"""PRK — Park, unpark and session close (ACTION park/unpark)."""
import pytest

from client import DeviceError


@pytest.fixture(autouse=True)
def _unpark(proto):
    yield
    proto.action("unpark")


def test_park_from_observation(proto):
    """Parking from observation stops the mount and sets at_park."""
    proto.control(tracking="sidereal")
    proto.action("park")
    assert proto.state()["at_park"] is True
    assert proto.state()["tracking"] == "none"


def test_park_again(proto):
    """Parking an already-parked mount is idempotent."""
    proto.action("park")
    assert proto.state()["at_park"] is True
    proto.action("park")
    assert proto.state()["at_park"] is True


def test_parked_blocks_motion(proto):
    """While parked, a goto is rejected."""
    proto.action("park")
    with pytest.raises(DeviceError):
        proto.action("goto", ra=6.0, dec=-60.0, speed=4)


def test_parked_rejects_all_commands(proto):
    """While parked only unpark is allowed; every other command is rejected."""
    proto.action("park")
    with pytest.raises(DeviceError):
        proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    with pytest.raises(DeviceError):
        proto.control(manual_ra_dps=1.0)
    with pytest.raises(DeviceError):
        proto.control(manual_dec_dps=1.0)
    with pytest.raises(DeviceError):
        proto.action("home")
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=100)
    with pytest.raises(DeviceError):
        proto.control(tracking="sidereal")

    proto.action("unpark")
    assert proto.state()["at_park"] is False


def test_unpark(proto):
    """Unparking returns to normal operation."""
    proto.action("park")
    assert proto.state()["at_park"] is True
    proto.action("unpark")
    assert proto.state()["at_park"] is False
    # now it can move
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    proto.action("stop")
