"""STATE — the coherent snapshot the app serves to its clients."""


def test_state_coherent(proto):
    """A single STATE read reports a valid state and tracking mode."""
    st = proto.state()
    assert st["state"] in ("ready", "slewing", "tracking", "parked", "error")
    assert st["tracking"] in ("none", "sidereal", "lunar", "solar")


def test_state_has_position_fields(proto):
    """STATE carries the authoritative int64 steps and the derived coordinates."""
    st = proto.state()
    for key in ("ra_steps", "dec_steps", "ra_axis_deg", "dec_axis_deg",
                "ra", "dec", "lst", "pier_side"):
        assert key in st, f"missing {key}"
    assert isinstance(st["ra_steps"], int)
    assert isinstance(st["dec_steps"], int)
    assert st["pier_side"] in ("east", "west")


def test_state_boot_id_stable(proto):
    """boot_id in STATE matches CAPABILITIES and is stable."""
    boot = proto.capabilities()["boot_id"]
    assert proto.state()["boot_id"] == boot


def test_state_flags_present(proto):
    """power / guiding / at_home / at_park / time_valid are reported, not guessed."""
    st = proto.state()
    assert isinstance(st["power"], bool)
    assert isinstance(st["guiding"], bool)
    assert isinstance(st["at_home"], bool)
    assert isinstance(st["time_valid"], bool)
    assert isinstance(st["at_park"], bool)


def test_state_site_config_rev_advances(proto):
    """After a CONFIG change, config_rev advances in both CONFIG and STATE."""
    before = proto.config_get()["config_rev"]
    proto.config_set(lat=-32.9, lon=-68.8, elevation=750)
    after = proto.config_get()["config_rev"]
    assert after > before
    # STATE carries the new config_rev too.
    assert proto.state()["config_rev"] == after
