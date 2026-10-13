# NRM-BFM test suite

Functional acceptance suite for an equatorial mount against real hardware,
driven through the **CDC-ACM serial protocol**.

## Structure

```text
test/proto/          serial protocol acceptance suite
  client.py          serial client (framing, request/response, error taxonomy)
  oracle.py          independent oracle (spherical angular distance + slew-time model)
  profile.json       versioned profile (budgets/tolerances)
  conftest.py        fixtures, bounded polling, axis-position/goto-duration helpers
  test_capabilities.py  identity + capabilities
  test_state.py         snapshot coherence and fields
  test_cfg.py           site, time, coordinates
  test_val.py           contract, invalid inputs, errors
  test_json.py          structural rejection of malformed JSON frames
  test_trk.py           tracking (rate coherence, limits)
  test_stp.py           stop and cancellation
  test_got*.py          slewing (GOTO): duration, speed, sequence, sites, limits, flip, guide
  test_man.py           manual movement (MoveAxis) + axis limits
  test_gui.py           autoguiding (PulseGuide)
  test_guide_effect.py  PulseGuide position/time effect
  test_mer.py           pier side / meridian flip
  test_prk.py           park / unpark
  test_ref.py           reference / home
  test_limits.py        runtime-changed limits
  test_actions.py       action family
  test_session.py       long mixed session
```

## Requirements

- NRM-BFM board connected over USB, flashed with the firmware (CDC-ACM).
  The suite auto-detects the serial port by USB VID/PID.
- Python 3 + `pytest`.

```sh
python3 -m pip install pytest
```

## Running

```sh
make -C test test-cdc     # the serial protocol suite
make -C test flash        # flash the firmware
```

Manual equivalent:

```sh
cd test/proto
python3 -m pytest -q            # the whole suite
python3 -m pytest --lf          # only the ones that failed last time
python3 -m pytest test_got.py   # one family
python3 -m pytest -k "park"     # by name
```

## Skill

There is a project skill, **`nrm-test`** (`.claude/skills/nrm-test/SKILL.md`),
that encapsulates this whole workflow: how to run the suite, how to recover the
board when the serial link is down, how to diagnose a failure (transport vs
device vs assertion, serial capture for crashes) and how to fix it without
weakening the test expectations.

In Claude Code, invoke it with:

```
/nrm-test
```

or just ask to "run the tests and fix any failure" and the skill triggers by its
description.

## Results

- No `skip`-marked cases: what can be exercised over the protocol runs; what
  requires physical acceptance (encoder, camera, power bench, N.I.N.A.) stays out
  of this automated suite and is neither declared passed nor pending.
- Each case uses an independent oracle (angular distance, slew-time ramp model)
  for the reported position and duration, not the value of the protocol itself.

## Profile

`profile.json` is the versioned profile. `null` fields block the tests that use
them; a pending value is not a PASS. `budgets.reported_position_deg` is the
tolerance for the reported position (not physical pointing).
