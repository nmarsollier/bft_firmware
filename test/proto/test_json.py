"""JSON — structural rejection of malformed request frames.

The firmware parses requests structurally (not with text search) and validates
numbers strictly.  A malformed frame — nested or duplicate keys, a negative or
fractional id/duration, trailing bytes — must be rejected with ok=false and
never execute an action.  These cases are sent as raw frames because the typed
client always builds well-formed JSON.
"""


def test_nested_type_not_dispatched(proto):
    """A `type` nested inside another object is not the request's type."""
    r = proto.raw_frame('{"extra":{"type":"action"},"id":1}')
    assert r["ok"] is False


def test_nested_action_not_dispatched(proto):
    """An `action` nested inside `extra` does not run the action."""
    r = proto.raw_frame('{"type":"action","id":1,"extra":{"action":"stop"}}')
    assert r["ok"] is False


def test_duplicate_id_rejected(proto):
    """A duplicated top-level key is rejected."""
    r = proto.raw_frame('{"type":"state","id":1,"id":2}')
    assert r["ok"] is False


def test_duplicate_unused_key_rejected(proto):
    """A duplicated key is rejected even when no handler consumes it."""
    r = proto.raw_frame('{"type":"state","foo":1,"foo":2}')
    assert r["ok"] is False


def test_negative_id_rejected(proto):
    """A negative id is rejected rather than wrapped to a large unsigned."""
    r = proto.raw_frame('{"type":"state","id":-1}')
    assert r["ok"] is False


def test_fractional_id_rejected(proto):
    """A fractional id is rejected."""
    r = proto.raw_frame('{"type":"state","id":1.5}')
    assert r["ok"] is False


def test_huge_id_rejected(proto):
    """An id overflowing uint32 is rejected."""
    r = proto.raw_frame('{"type":"state","id":4294967296}')
    assert r["ok"] is False


def test_negative_duration_rejected(proto):
    """A negative duration_ms is rejected (regression: -4294967295 wrapped to 1)."""
    r = proto.raw_frame(
        '{"type":"action","id":1,"action":"guide","direction":"north","duration_ms":-4294967295}'
    )
    assert r["ok"] is False


def test_trailing_bytes_rejected(proto):
    """A second object after the request is rejected."""
    r = proto.raw_frame('{"type":"state"}}')
    assert r["ok"] is False


def test_channel_recovers_after_invalid(proto):
    """An invalid frame does not leave the channel unusable."""
    for body in (
        '{"type":"state","id":1,"id":2}',
        '{"type":"state","id":-1}',
        '{"extra":{"type":"action"},"id":1}',
    ):
        assert proto.raw_frame(body)["ok"] is False
    assert proto.capabilities()["name"] == "BFT"
