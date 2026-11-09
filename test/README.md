# BFT test suite

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
  test_got*.py          slewing (GOTO): duration, speed, sequence, sites, limits, meridian, guide
  test_man.py           manual movement (MoveAxis) + axis limits
  test_gui.py           autoguiding (PulseGuide)
  test_guide_effect.py  PulseGuide position/time effect
  test_trk_preserve.py  tracking never lost across axis movements + guide pulses
  test_phd_calibration.py  end-to-end PHD2 calibration replay (counters move)
  test_mer.py           meridian crossing without flip
  test_prk.py           park / unpark
  test_ref.py           reference / home
  test_limits.py        runtime-changed limits
  test_actions.py       action family
  test_session.py       long mixed session
```

## Requirements

- BFT board connected over USB, flashed with the firmware (CDC-ACM).
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

There is a project skill, **`bft-test`** (`.claude/skills/bft-test/SKILL.md`),
that encapsulates this whole workflow: how to run the suite, how to recover the
board when the serial link is down, how to diagnose a failure (transport vs
device vs assertion, serial capture for crashes) and how to fix it without
weakening the test expectations.

In Claude Code, invoke it with:

```
/bft-test
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

## Host regressions (no board movement)

`python3 -m unittest discover -s test/host -v` from the repository root compiles
and exercises the production coordinate and guiding C code with hardware
stubs. It covers meridian continuity, orientation, limits, poles and guiding.
The CDC suite still requires a board and is the integration acceptance step.

The PHD2 calibration host test is `test/host/test_phd_calibration.py`. It
compiles the **real** production guide path (`mount_pulse_guide.c`,
`motors_pulse_guide.c`, `motors_command_queue.c`, `motors_rmt.c`, `motors_hw.c`,
`motors_current_state.c`) against a simulated clock/RTOS/RMT and **observes the
STEP transmissions** per axis. A "suppress DEC RMT" mutation makes the nominal
DEC pulse emit zero steps, so a motionless DEC is caught even though every
layer above returns `ok=true`.

## PHD2 calibration regression — result separation

The motivating bug was a real calibration where DEC did not move while all
commands answered `ok=true`. Coverage is split so each layer is honest about
what it can prove:

| Layer | Test | What it proves |
|---|---|---|
| Host (RMT observation) | `test/host/test_phd_calibration.py` | The motion task actually emits DEC STEP pulses with the right sign/shape; suppressed DEC RMT must fail. |
| CDC (counters) | `test/proto/test_phd_calibration.py` | The firmware's reported `ra_steps`/`dec_steps` move by the guide rate over the serial protocol. |
| Gateway/PHD2 | not automated here | A CDC replay is not PHD2; it needs the real `mount_panel` Alpaca translation (see its own suite). |
| Physical motion | out of scope | Neither counters nor RMT confirm the motor/axis turned; that needs a camera/encoder. |

The nominal CDC sequence is a **single test** (`test_phd2_calibration_nominal`)
so the autouse STOP/HOME fixtures cannot interleave and destroy the
tracking/motion state being asserted. It drives W→E→DEC-cleanup→N→S with
sidereal tracking and checks the DEC arc magnitude with an independent oracle
(`GUIDE_DPS = 0.5 × sidereal`, `DEG_PER_STEP = 1.8/(32·500)`), not with a
0.1° tolerance that would pass a motionless axis.
