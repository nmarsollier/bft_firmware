"""ACTIONS adicionales — move relativo y límites (agregados al protocolo)."""
import pytest

from conftest import axis_position, wait_not_moving


@pytest.fixture(autouse=True)
def _settle(proto):
    yield
    proto.action("stop")
    proto.control(tracking="none")


def test_move_relative_ra(proto):
    """ACTION move mueve un eje una cantidad relativa de grados."""
    proto.action("home")
    wait_not_moving(proto)
    before = axis_position(proto)["ra_deg"]
    proto.action("move", axis="ra", degrees=5.0, speed=4)
    wait_not_moving(proto)
    after = axis_position(proto)["ra_deg"]
    assert abs(after - before) > 4.0, f"RA moved {after - before:.2f}°, expected ~5°"


def test_move_relative_dec(proto):
    """ACTION move funciona en DEC (eje relativo)."""
    proto.action("home")
    wait_not_moving(proto)
    before = axis_position(proto)["dec_deg"]
    proto.action("move", axis="dec", degrees=-5.0, speed=4)
    wait_not_moving(proto)
    after = axis_position(proto)["dec_deg"]
    assert abs(after - before) > 4.0, f"DEC moved {after - before:.2f}°, expected ~5°"


def test_move_rejects_bad_axis(proto):
    """ACTION move con un eje inválido es rechazado."""
    from client import DeviceError
    with pytest.raises(DeviceError):
        proto.action("move", axis="x", degrees=5.0, speed=4)


def test_limits_set_home(proto):
    """ACTION limits set_home marca la posición actual como home."""
    proto.action("home")
    wait_not_moving(proto)
    proto.action("move", axis="ra", degrees=10.0, speed=4)
    wait_not_moving(proto)
    proto.action("limits", param="set_home")
    assert proto.state()["at_home"] is True


def test_limits_clear(proto):
    """ACTION limits clear_limits restaura los límites de fábrica."""
    proto.action("limits", param="clear_limits")
    st = proto.state()
    assert st["limits"]["ra_min"] < st["limits"]["ra_max"]
    assert st["limits"]["dec_min"] < st["limits"]["dec_max"]
