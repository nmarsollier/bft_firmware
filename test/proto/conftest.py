"""Fixtures and helpers for the BFT serial protocol suite.

The suite drives the mount through the CDC-ACM protocol (request/response).
The ESP is the authority over the mount: it publishes a coherent state and
executes a small set of operations; this suite validates that behaviour over
the serial link.
"""
import json
import os
import time

import pytest

from client import ProtoClient, detect_serial_port
from oracle import angular_separation_deg, goto_time

PROFILE_PATH = os.path.join(os.path.dirname(__file__), "profile.json")


class Profile(dict):
    @classmethod
    def load(cls, path=PROFILE_PATH):
        with open(path) as f:
            return cls(json.load(f))

    def require(self, *keys):
        missing = [k for k in keys if self.get(k) is None]
        if missing:
            pytest.fail(f"Perfil con valores pendientes para: {missing}")
        return self


@pytest.fixture(scope="session")
def profile():
    p = Profile.load()
    # Resolution order: NRM_SERIAL env (explicit) → USB VID/PID auto-detect →
    # profile.json fallback.
    port = os.environ.get("NRM_SERIAL") or detect_serial_port() or p.get("serial_port")
    if port:
        p["serial_port"] = port
    else:
        pytest.fail(
            "No BFT CDC serial port found. Connect the board, flash the "
            "firmware (`make flash`), then re-run."
        )
    return p


@pytest.fixture(scope="session")
def proto(profile):
    return ProtoClient(profile["serial_port"])


# ── bounded polling ───────────────────────────────────────────────

def wait_until(getter, pred, timeout, desc, poll=0.25):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = getter()
        if pred(last):
            return last
        time.sleep(poll)
    raise AssertionError(f"timeout waiting for {desc} (last={last!r})")


def wait_state(proto, pred, timeout, desc):
    return wait_until(lambda: proto.state()["state"], pred, timeout, desc)


def wait_slewing(proto, timeout=10):
    """Wait until the mount reports SLEWING (a command was picked up)."""
    return wait_state(proto, lambda s: s == "slewing", timeout, "state=slewing")


def wait_not_moving(proto, timeout=90):
    """Wait until the mount is idle and stays idle across one poll.

    A command is accepted before the motion task picks it up, so a single
    "ready" read right after the command would race and return too early.
    Requiring the idle state to survive one poll avoids that.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proto.state()["state"] in ("ready", "tracking", "parked"):
            time.sleep(0.25)
            if proto.state()["state"] in ("ready", "tracking", "parked"):
                return
        else:
            time.sleep(0.25)
    raise AssertionError("timeout waiting for mount to stop moving")


# ── site/time setup (GOTO requirement) ────────────────────────────

def setup_site_time(proto):
    """Set a valid site and UTC via the protocol to enable GOTO."""
    proto.config_set(lat=-32.89, lon=-68.83, elevation=750,
                     utc="2026-09-28T02:00:00Z")


# ── celestial / axis helpers ───────────────────────────────────────

def assert_angular_close(ra1, dec1, ra2, dec2, tol_deg, ctx=""):
    """Spherical angular distance between two equatorial positions."""
    sep = angular_separation_deg(ra1, dec1, ra2, dec2)
    assert sep <= tol_deg, f"{ctx} separation {sep:.4f}° > tolerance {tol_deg}°"


def axis_position(proto):
    """Physical axis position from the protocol STATE snapshot."""
    st = proto.state()
    return {
        "ra_deg": st["ra_axis_deg"],
        "dec_deg": st["dec_axis_deg"],
        "ra_steps": st["ra_steps"],
        "dec_steps": st["dec_steps"],
    }


def goto_and_arrive(proto, ra, dec, speed=4):
    """Issue a goto and wait until the mount is idle again."""
    proto.action("goto", ra=ra, dec=dec, speed=speed)
    wait_not_moving(proto)


def wait_tracking(proto, mode, timeout=10):
    """Wait until the protocol reports the given tracking mode."""
    return wait_until(lambda: proto.state()["tracking"],
                      lambda t: t == mode, timeout, f"tracking={mode}")


def assert_goto_duration(proto, ra, dec, speed_dps, slack_s=3.0, factor=1.35):
    """Issue a goto and assert the wall-clock duration matches the slew model.

    Measures the observed axis distance and the commanded speed, then checks
    the move time is within the ramp model band and that the mount arrived at
    the requested destination.
    """
    start = axis_position(proto)
    t0 = time.monotonic()
    proto.action("goto", ra=ra, dec=dec, speed=4)   # speed 4 == 4 °/s
    wait_not_moving(proto)
    elapsed = time.monotonic() - t0
    end = axis_position(proto)

    ra_dist = abs(end["ra_deg"] - start["ra_deg"])
    dec_dist = abs(end["dec_deg"] - start["dec_deg"])
    expected = goto_time(ra_dist, dec_dist, speed_dps)

    st = proto.state()
    assert_angular_close(st["ra"], st["dec"], ra, dec, 0.1, "goto arrival")

    lower = expected * 0.5
    upper = expected * factor + slack_s
    assert elapsed >= lower, \
        f"goto finished in {elapsed:.2f}s, suspiciously < {lower:.2f}s"
    assert elapsed <= upper, \
        f"goto took {elapsed:.2f}s, more than expected {upper:.2f}s"
    return {"elapsed": elapsed, "expected": expected,
            "ra_dist": ra_dist, "dec_dist": dec_dist,
            "final_ra": st["ra"], "final_dec": st["dec"],
            "start_axis": start, "end_axis": end}


# ── connection fixture ────────────────────────────────────────────

@pytest.fixture(scope="module", autouse=True)
def _home_each_module(proto):
    """Home once per test module so motion tests start from a known position.

    Eliminates order-dependence: a move/stop test that assumes the mount is away
    from the axis limits must not depend on where the previous file left it.
    Also resets the limits, which tests mutate and a previous run may have left
    persisted in NVS.
    """
    try:
        proto.action("unpark")
        proto.action("stop")
        proto.action("home")
        wait_not_moving(proto, timeout=40)
        proto.action("limits", param="clear_limits")
    except Exception:
        pass


@pytest.fixture(autouse=True)
def _settle(proto):
    """Start every test from a known READY state (no motion, no manual rates)."""
    try:
        proto.action("stop")
        proto.control(manual_ra_dps=0.0, manual_dec_dps=0.0)
    except Exception:
        pass   # the link may be stale after a reconnect
    yield
    try:
        proto.action("stop")
        proto.control(manual_ra_dps=0.0, manual_dec_dps=0.0)
    except Exception:
        pass
