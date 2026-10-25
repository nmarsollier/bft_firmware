"""REF — Reference, home and position recovery (ACTION home)."""
import pytest

from conftest import (assert_angular_close, axis_position, setup_site_time,
                      wait_not_moving, wait_slewing, wait_tracking)

MODES = {0: "sidereal", 1: "lunar", 2: "solar"}


@pytest.fixture(autouse=True)
def _unpark(proto):
    yield
    proto.action("unpark")


def test_home_park_celestial_distinct(proto):
    """at_home and at_park are booleans and distinct concepts."""
    st = proto.state()
    assert isinstance(st["at_home"], bool)
    assert isinstance(st["at_park"], bool)


def test_find_home_from_position(proto):
    """ACTION home moves the mount to the physical origin."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_not_moving(proto)

    proto.action("home")
    wait_not_moving(proto)
    assert proto.state()["at_home"] is True


def test_find_home_when_already_home(proto):
    """ACTION home when already home causes no excursion."""
    proto.action("home")
    wait_not_moving(proto)
    assert proto.state()["at_home"] is True


@pytest.mark.parametrize("ra,dec", [(6.0, -60.0), (5.0, -45.0), (7.0, -70.0)])
def test_find_home_from_various_positions(proto, ra, dec):
    """ACTION home returns to axis (0,0) from different sky positions."""
    setup_site_time(proto)
    proto.action("goto", ra=ra, dec=dec, speed=4)
    wait_not_moving(proto)

    proto.action("home")
    wait_not_moving(proto)
    pos = axis_position(proto)
    assert abs(pos["ra_deg"]) < 1.0 and abs(pos["dec_deg"]) < 1.0, \
        f"home no es (0,0): ra={pos['ra_deg']} dec={pos['dec_deg']}"


@pytest.mark.parametrize("rate", [0, 1, 2])
def test_find_home_in_tracking(proto, rate):
    """ACTION home from each tracking mode returns home and clears tracking."""
    setup_site_time(proto)
    proto.control(tracking=MODES[rate])
    wait_tracking(proto, MODES[rate])

    proto.action("home")
    wait_not_moving(proto)
    pos = axis_position(proto)
    assert abs(pos["ra_deg"]) < 1.0 and abs(pos["dec_deg"]) < 1.0
    assert proto.state()["tracking"] == "none", "home should not resume tracking"


def test_find_home_mid_goto(proto):
    """ACTION home issued during a goto stops the goto and returns home."""
    setup_site_time(proto)
    proto.action("goto", ra=6.0, dec=-60.0, speed=4)
    wait_slewing(proto)

    proto.action("home")   # mid-goto
    wait_not_moving(proto)
    pos = axis_position(proto)
    assert abs(pos["ra_deg"]) < 1.0 and abs(pos["dec_deg"]) < 1.0, \
        f"home mid-goto no volvió a (0,0): ra={pos['ra_deg']} dec={pos['dec_deg']}"


def test_home_interrupted_reports_correct_coordinates(proto):
    """A home cancelled mid-slew leaves no orientation state: a later GOTO
    resolves from the actually-reached position and reports the requested
    coordinates (the reported pointing is correct even though home was
    interrupted with a negative DEC)."""
    setup_site_time(proto)
    proto.action("home")
    wait_not_moving(proto)

    # Move DEC through the pole to the negative side (dec_axis < 0).
    proto.action("move", axis="dec", degrees=-30.0, speed=4)
    wait_not_moving(proto)
    assert proto.state()["dec_axis_deg"] < -20, \
        f"DEC not on the negative side: {proto.state()['dec_axis_deg']}"

    # Home starts, then is cancelled before it completes.
    proto.action("home")
    wait_slewing(proto)
    proto.action("stop")
    wait_not_moving(proto)

    # The GOTO resolves from the reached position (wherever the stop landed)
    # and arrives at the requested coordinates; there is no remembered side to
    # leak into the result.
    lst = proto.state()["lst"]
    ra = (lst + 4.0) % 24.0
    proto.action("goto", ra=ra, dec=-60.0, speed=4)
    wait_not_moving(proto)
    st = proto.state()
    assert_angular_close(st["ra"], st["dec"], ra, -60.0, 0.1,
                         "after interrupted home")
