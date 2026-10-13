"""GOT-speed — the goto duration scales with the requested speed."""
import time

import pytest

from conftest import axis_position, goto_time, setup_site_time, wait_not_moving
from oracle import MIN_SLEW_DPS

SPEEDS = [(1, 1.0), (2, 3.0), (3, 4.5), (4, 6.0)]


@pytest.fixture(autouse=True)
def _quiet(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def _measure(proto, goto_fn):
    start = axis_position(proto)
    t0 = time.monotonic()
    goto_fn()
    wait_not_moving(proto)
    elapsed = time.monotonic() - t0
    end = axis_position(proto)
    return elapsed, abs(end["ra_deg"] - start["ra_deg"]), abs(end["dec_deg"] - start["dec_deg"])


@pytest.mark.parametrize("speed_rate,speed_dps", SPEEDS)
def test_goto_speed_duration(proto, speed_rate, speed_dps):
    """The goto time matches the ramp model for the requested speed."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-55.0, speed=4)
    wait_not_moving(proto)

    elapsed, ra_dist, dec_dist = _measure(
        proto, lambda: proto.action("goto", ra=6.0, dec=-60.0, speed=speed_rate))
    expected = goto_time(ra_dist, dec_dist, speed_dps)
    assert elapsed >= expected * 0.6, f"goto too fast: {elapsed:.2f}s < {expected*0.6:.2f}s"
    assert elapsed <= expected * 1.4 + 3.0, \
        f"goto too slow: {elapsed:.2f}s > {expected*1.4+3.0:.2f}s"


@pytest.mark.parametrize("delta", [1.0, 3.0, 5.0])
def test_small_goto_time(proto, delta):
    """A small DEC goto of `delta` degrees takes the model time."""
    setup_site_time(proto)
    lst = proto.state()["lst"]
    ra = (lst - 4.0) % 24.0   # pier side unambiguous
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)

    start = axis_position(proto)
    t0 = time.monotonic()
    proto.action("goto", ra=ra, dec=-60.0 - delta, speed=4)
    wait_not_moving(proto)
    elapsed = time.monotonic() - t0
    end = axis_position(proto)

    dec_dist = abs(end["dec_deg"] - start["dec_deg"])
    assert abs(dec_dist - delta) < 1.0, \
        f"small goto moved DEC {dec_dist:.2f}° (expected ~{delta}°)"

    const_time = dec_dist / MIN_SLEW_DPS
    if delta < 2.0:
        assert abs(elapsed - const_time) <= 0.75, \
            f"<2° goto should be constant {MIN_SLEW_DPS}°/s (~{const_time:.2f}s), got {elapsed:.2f}s"
    else:
        assert elapsed < const_time, \
            f">2° goto should ramp (faster than {const_time:.2f}s), got {elapsed:.2f}s"
