"""GOT-sites — the same goto resolves correctly from different site coordinates."""
import pytest

from conftest import assert_goto_duration


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


@pytest.mark.parametrize("lat,lon", [
    (-32.89, -68.83),   # southern, home site
    (-23.55, -46.63),   # southern, different longitude
    (40.71, -74.01),    # northern
])
def test_goto_across_sites(proto, lat, lon):
    proto.config_set(lat=lat, lon=lon, elevation=750,
                     utc="2026-09-28T02:00:00Z")
    # Target on the meridian (HA=0): reachable at every site regardless of the
    # RA ±120 limit (a fixed ra=6h would fall outside ±120 at lon -74°).
    lst = proto.state()["lst"]
    assert_goto_duration(proto, lst, -60.0, 6.0)
