"""CFG — Site, time and coordinates (CONFIG family)."""
import time


def test_set_site(profile, proto):
    """Set the observing site via CONFIG and read it back."""
    proto.config_set(lat=-32.89, lon=-68.83, elevation=750)
    cfg = proto.config_get()["config"]
    assert abs(cfg["lat"] - (-32.89)) < 0.01
    assert abs(cfg["lon"] - (-68.83)) < 0.01
    assert cfg["elevation"] == 750


def test_set_utc_date(proto):
    """Set UTC via CONFIG; STATE.time_valid becomes true."""
    proto.config_set(utc="2026-09-28T02:00:00Z")
    assert proto.state()["time_valid"] is True


def test_set_utc_invalid_rejected(proto):
    """An invalid UTC string is rejected (ok=false), not applied."""
    import pytest

    from client import DeviceError

    for bad in ("2026-02-30T00:00:00Z", "not-a-date", ""):
        with pytest.raises(DeviceError):
            proto.config_set(utc=bad)


def test_config_echoes_utc(proto):
    """CONFIG's response returns the current system clock as ISO 8601 UTC."""
    from datetime import datetime, timedelta, timezone

    set_at = datetime(2026, 9, 28, 2, 0, 0, tzinfo=timezone.utc)
    proto.config_set(utc="2026-09-28T02:00:00Z")

    utc = proto.config_get()["utc"]
    got = datetime.fromisoformat(utc)  # 3.11+ parses the trailing "Z"
    assert got >= set_at
    assert got - set_at < timedelta(seconds=10)


def test_sidereal_time_coherent(proto):
    """LST is numeric in [0,24) and advances with time (24h wrap)."""
    proto.config_set(utc="2026-09-28T02:00:00Z")
    lst1 = proto.state()["lst"]
    time.sleep(1.1)
    lst2 = proto.state()["lst"]
    assert 0 <= lst1 < 24
    assert (lst2 - lst1) % 24 > 0, f"LST did not advance: {lst1} -> {lst2}"


def test_change_site_during_motion(proto):
    """An invalid site change during motion is rejected without partial effect."""
    from conftest import setup_site_time
    from client import DeviceError
    import pytest

    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)

    # while slewing, an invalid latitude is rejected and the site is unchanged
    with pytest.raises(DeviceError):
        proto.config_set(lat=120.0)
    cfg = proto.config_get()["config"]
    assert abs(cfg["lat"] - (-32.89)) < 0.01

    proto.action("stop")
