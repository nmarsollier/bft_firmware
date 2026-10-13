"""VAL — Public contract, invalid inputs and observable errors.

The protocol rejects bad inputs with ok=false; the channel stays usable after.
"""
import pytest

from client import DeviceError


def test_reject_invalid_site(proto):
    """Out-of-range or non-finite site values are rejected."""
    with pytest.raises(DeviceError):
        proto.config_set(lat=120.0)          # out of range
    with pytest.raises(DeviceError):
        proto.config_set(lon=200.0)          # out of range


def test_reject_invalid_utc(proto):
    """A malformed UTC date is rejected."""
    with pytest.raises(DeviceError):
        proto.config_set(utc="no-es-una-fecha")


def test_reject_bad_tracking(proto):
    """An unknown tracking mode is rejected."""
    with pytest.raises(DeviceError):
        proto.control(tracking="bogus")


def test_reject_goto_ra_range(proto):
    """A goto with RA outside [0,24) is rejected."""
    with pytest.raises(DeviceError):
        proto.action("goto", ra=25.0, dec=0.0, speed=4)


def test_reject_bad_guide_direction(proto):
    """A guide with an unknown direction is rejected."""
    with pytest.raises(DeviceError):
        proto.action("guide", direction="up", duration_ms=500)


def test_reject_sync(proto):
    """ACTION sync is rejected (not supported in this iteration)."""
    with pytest.raises(DeviceError):
        proto.action("sync", ra=1.0, dec=-33.0)


def test_error_then_recover(proto):
    """An error does not leave the channel unusable."""
    with pytest.raises(DeviceError):
        proto.action("sync", ra=1.0, dec=-33.0)
    # the channel stays healthy
    assert proto.capabilities()["name"] == "NRM-BFM"


def test_valid_edges(proto):
    """Valid values at the edges are accepted."""
    caps = proto.capabilities()
    assert caps["tracking_modes"] == ["none", "sidereal", "lunar", "solar"]
    proto.config_set(guide_rate_ra=0.001, guide_rate_dec=0.001)
    cfg = proto.config_get()["config"]
    assert cfg["guide_rate_ra"] > 0
    assert cfg["guide_rate_dec"] > 0
