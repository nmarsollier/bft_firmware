"""CAPABILITIES — device identity and capabilities for the gateway translation."""


def test_capabilities_identity(proto):
    """CAPABILITIES reports identity and protocol version."""
    h = proto.capabilities()
    assert h["proto"] == 1
    assert h["name"] == "BFT"
    assert h["axes"] == ["ra", "dec"]
    assert isinstance(h["boot_id"], int) and h["boot_id"] != 0


def test_capabilities_slew_speeds(proto):
    """CAPABILITIES reports the four slew speed profiles in deg/s."""
    speeds = proto.capabilities()["slew_speeds_dps"]
    assert len(speeds) == 4
    assert speeds == [0.5, 1.0, 2.5, 4.0]


def test_capabilities_tracking_modes(proto):
    """CAPABILITIES reports the supported tracking modes."""
    modes = proto.capabilities()["tracking_modes"]
    assert modes == ["none", "sidereal", "lunar", "solar"]


def test_capabilities_flags(proto):
    """CAPABILITIES reports the capabilities the gateway app must advertise."""
    caps = proto.capabilities()["caps"]
    # Hard capabilities the firmware implements.
    assert caps["guide"] is True
    assert caps["home"] is True
    assert caps["park"] is True
    assert caps["unpark"] is True
    assert caps["move_axis"] is True
    assert caps["slew"] is True
    assert caps["tracking"] is True
    # Sync is deferred in this iteration.
    assert caps["sync"] is False
    # Fork mount: no meridian flip.
    assert caps["meridian_flip"] is False


def test_capabilities_boot_id_stable(proto):
    """boot_id is stable across requests within a boot."""
    assert proto.capabilities()["boot_id"] == proto.capabilities()["boot_id"]
