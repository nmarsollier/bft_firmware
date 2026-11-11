"""PHD2-CAL — end-to-end PulseGuide calibration regression over CDC-ACM.

Reproduces the PulseGuide sequence a PHD2 calibration drives, and — unlike the
acceptance tests in test_gui.py — asserts the reported *counters actually move*
by the guide rate.  The original failure was a motionless DEC axis while every
command answered ok=true; this test detects that by measuring ra_steps/dec_steps
against an independent oracle.

The nominal sequence is a single test so the interleaved STOP/HOME of the
autouse fixtures cannot destroy the tracking/motion state under test.

Run (requires the board over CDC-ACM):
    make -C test test-cdc     # or: cd test/proto && python3 -m pytest test_phd_calibration.py
"""
import time

import pytest

from client import DeviceError
from conftest import setup_site_time, wait_not_moving, wait_tracking, wait_until

# Independent oracle — never derived from the firmware's own motion functions.
SIDEREAL_DPS = 0.004178074
GUIDE_DPS = 0.5 * SIDEREAL_DPS        # 0.002089037 deg/s (NOT 0.5 deg/s)
DEG_PER_STEP = 1.8 / (64 * 500)       # 0.00005625 deg/microstep


def _expected_steps(duration_ms):
    return GUIDE_DPS * duration_ms / 1000.0 / DEG_PER_STEP


def _snapshot(proto):
    """State plus wall-clock, bracketed by the request round-trip so the RA
    tracking baseline can be bounded by its own uncertainty."""
    t0 = time.monotonic()
    st = proto.state()
    t1 = time.monotonic()
    return st, t0, t1


def _assert_tracking(proto):
    st = proto.state()
    assert st["state"] in ("tracking", "ready"), f"unexpected state {st['state']}"
    assert st["tracking"] == "sidereal", f"tracking lost: {st['tracking']}"
    assert st["guiding"] is False, "left pulse-guiding active"


def _pulse_and_observe(proto, direction, duration_ms):
    """One nominal calibration pulse: verify preconditions, observe guiding
    start/end, and return the (ra_delta, dec_delta) step deltas.

    ACK means enqueued, not completed: for a 500 ms pulse the guiding flag must
    be observed true, and completion must fall within the pulse deadline plus a
    bounded start/polling budget."""
    _assert_tracking(proto)
    before = proto.state()
    ra0, dec0 = before["ra_steps"], before["dec_steps"]
    boot0 = before["boot_id"]

    proto.action("guide", direction=direction, duration_ms=duration_ms)
    t_start = time.monotonic()

    if duration_ms >= 500:
        wait_until(lambda: proto.state()["guiding"], lambda g: g is True,
                   duration_ms / 1000.0 + 1.0, f"guiding=true during {direction}")

    # Wait out the pulse deadline + start budget; the pulse must then be over.
    while time.monotonic() < t_start + duration_ms / 1000.0 + 0.5:
        time.sleep(0.05)
    assert proto.state()["guiding"] is False, f"guiding stuck true after {direction}"

    after = proto.state()
    assert after["boot_id"] == boot0, "firmware rebooted mid-sequence"
    _assert_tracking(proto)

    return after["ra_steps"] - ra0, after["dec_steps"] - dec0


def _run_phase(proto, direction, count, duration_ms):
    """Run `count` identical pulses; return (ra_delta, dec_delta, elapsed_s).

    Displacement and elapsed are measured between the SAME snapshots — the one
    before the first pulse and the one after the last.  Elapsed uses the
    firmware's `uptime_ms` (its own monotonic clock) so the tracking base can be
    subtracted precisely, not the test's wall clock.
    """
    before = proto.state()
    ra0, dec0 = before["ra_steps"], before["dec_steps"]
    up0 = before["uptime_ms"]
    for _ in range(count):
        _pulse_and_observe(proto, direction, duration_ms)
        time.sleep(1.0)  # simulated exposure interval between pulses
    after = proto.state()
    ra1, dec1 = after["ra_steps"], after["dec_steps"]
    up1 = after["uptime_ms"]
    return ra1 - ra0, dec1 - dec0, (up1 - up0) / 1000.0


def _setup_calibration(proto):
    """Prepare the bench: known reference, cleared path, tracking on.  Every
    precondition is asserted — a failed setup must stop the case, not proceed."""
    h = proto.capabilities()
    assert h["device_id"] == "bft", f"wrong device {h.get('device_id')}"
    assert h["device_type"] == "mount", f"wrong type {h.get('device_type')}"
    assert h["caps"]["guide"] is True, "guide not advertised"
    boot_id = h["boot_id"]

    st = proto.state()
    assert st["boot_id"] == boot_id

    proto.action("unpark")
    proto.action("stop", scope="all")
    wait_not_moving(proto)

    # Reference: wait for the counters to actually reach 0, not just the ACK.
    proto.action("home")
    wait_until(lambda: proto.state()["ra_steps"], lambda v: v == 0, 40, "home ra==0")
    wait_until(lambda: proto.state()["dec_steps"], lambda v: v == 0, 40, "home dec==0")

    proto.config_set(lat=-32.89, lon=-68.83, elevation=750,
                     utc="2026-11-09T03:00:00Z",
                     guide_rate_ra=GUIDE_DPS, guide_rate_dec=GUIDE_DPS)
    cfg = proto.config_get()["config"]
    assert cfg["guide_rate_ra"] > 0 and cfg["guide_rate_dec"] > 0
    assert abs(cfg["guide_rate_ra"] - GUIDE_DPS) <= 0.0000005
    assert abs(cfg["guide_rate_dec"] - GUIDE_DPS) <= 0.0000005
    assert proto.state()["time_valid"] is True

    # Move off the meridian and onto the celestial equator (D = +90 mechanical).
    proto.action("move", axis="ra", degrees=5.0, speed=4)
    wait_not_moving(proto)
    proto.action("move", axis="dec", degrees=90.0, speed=4)
    wait_not_moving(proto)

    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")
    assert proto.state()["guiding"] is False

    # Baseline: RA advances at sidereal, DEC stays put.
    ra0 = proto.state()["ra_steps"]
    dec0 = proto.state()["dec_steps"]
    time.sleep(2.0)
    st = proto.state()
    assert st["ra_steps"] > ra0, "RA is not tracking"
    assert st["dec_steps"] == dec0, "DEC drifted while tracking"

    return dec0


def test_phd2_calibration_nominal(proto):
    """Full PHD2 calibration replay: W→E→DEC-cleanup→N→S with tracking on.

    The invariant is that DEC actually moves by GUIDE_DPS per pulse and that
    tracking survives the whole sequence."""
    dec_baseline = _setup_calibration(proto)
    ra_rate = SIDEREAL_DPS / DEG_PER_STEP   # steps/s of pure sidereal tracking
    guide_steps = _expected_steps(500)      # ~9.2846 steps per 500 ms pulse

    # ── W: 12 × west 500 ms — net RA correction +111 steps, DEC untouched ──
    ra_w, dec_w, t_w = _run_phase(proto, "west", 12, 500)
    assert dec_w == 0, f"DEC contaminated during west phase: {dec_w} steps"
    ra_w_corr = ra_w - ra_rate * t_w
    assert abs(ra_w_corr - 12 * guide_steps) <= 3, \
        f"west correction {ra_w_corr:.1f} != ~{12 * guide_steps:.1f} steps"

    # ── E: 12 × east 500 ms — net RA correction -111 steps, DEC untouched ──
    ra_e, dec_e, t_e = _run_phase(proto, "east", 12, 500)
    assert dec_e == 0, f"DEC contaminated during east phase: {dec_e} steps"
    ra_e_corr = ra_e - ra_rate * t_e
    assert abs(ra_e_corr - (-12 * guide_steps)) <= 3, \
        f"east correction {ra_e_corr:.1f} != ~{-12 * guide_steps:.1f} steps"

    # ── DEC cleanup + N: 3 north cleanup, 3rd is the 1st N step ──
    # Define the DEC calibration origin just before the 3rd cleanup pulse.
    for _ in range(2):
        _pulse_and_observe(proto, "north", 500)
        time.sleep(1.0)
    dec_origin = proto.state()["dec_steps"]
    _, dec_n, _ = _run_phase(proto, "north", 12, 500)  # 3rd cleanup + 11 more
    expected_n = 12 * guide_steps
    assert abs(dec_n - expected_n) <= 3, f"north arc {dec_n} steps != ~{expected_n:.1f}"
    assert dec_n > 0

    # ── S: 12 × south 500 ms — reverses and returns to the DEC origin ──
    _, dec_s, _ = _run_phase(proto, "south", 12, 500)
    expected_s = 12 * guide_steps
    assert abs(abs(dec_s) - expected_s) <= 3, f"south arc {dec_s} steps != ~-{expected_s:.1f}"
    assert dec_s < 0
    assert abs(proto.state()["dec_steps"] - dec_origin) <= 3, \
        "DEC did not return to the calibration origin"

    # ── post-guide corrections: no overlap, tracking preserved ──
    for direction, duration in (("north", 250), ("east", 750),
                                ("south", 250), ("west", 750),
                                ("north", 1000), ("south", 1000)):
        _pulse_and_observe(proto, direction, duration)
        time.sleep(0.5)

    # ── stop: guiding false, tracking none, counters stop ──
    proto.action("stop", scope="all")
    wait_until(lambda: proto.state()["tracking"], lambda t: t == "none", 5, "tracking=none")
    st1 = proto.state()
    time.sleep(1.0)
    st2 = proto.state()
    assert st1["ra_steps"] == st2["ra_steps"], "RA kept moving after stop"
    assert st1["dec_steps"] == st2["dec_steps"], "DEC kept moving after stop"


# ── guide-rate validation over the protocol ────────────────────────

def test_guide_rate_config_validation(proto):
    """CONFIG rejects out-of-range/non-finite rates without partial effects."""
    cfg0 = proto.config_get()["config"]
    with pytest.raises(DeviceError):
        proto.config_set(guide_rate_dec=1.5)
    with pytest.raises(DeviceError):
        proto.config_set(guide_rate_ra=-0.5)
    # One valid + one invalid field: the valid one must not be applied.
    with pytest.raises(DeviceError):
        proto.config_set(guide_rate_ra=0.25, guide_rate_dec=2.0)
    cfg = proto.config_get()["config"]
    assert cfg["guide_rate_ra"] == cfg0["guide_rate_ra"], "partial guide_rate_ra effect"
    assert cfg["guide_rate_dec"] == cfg0["guide_rate_dec"], "partial guide_rate_dec effect"


def test_zero_guide_rate_disables_axis(proto):
    """guide_rate=0 disables guiding on that axis; the other axis still guides."""
    proto.config_set(guide_rate_dec=0.0)
    try:
        with pytest.raises(DeviceError):
            proto.action("guide", direction="north", duration_ms=500)
        proto.action("guide", direction="west", duration_ms=100)
    finally:
        proto.config_set(guide_rate_dec=GUIDE_DPS)  # restore the default


def test_north_at_pole_rejected_no_steps(proto):
    """home → tracking → north is rejected and DEC emits no steps."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)
    proto.control(tracking="sidereal")
    wait_tracking(proto, "sidereal")

    dec0 = proto.state()["dec_steps"]
    with pytest.raises(DeviceError):
        proto.action("guide", direction="north", duration_ms=500)
    assert proto.state()["dec_steps"] == dec0, "DEC moved on a rejected north"
