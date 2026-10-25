# BFT Mount — Mechanical Geometry and Theoretical Operation

## 1. Purpose

This document defines the intended mechanical and mathematical behavior of the BFT telescope mount. It is a reference for future firmware changes and bug fixes.

It describes the intended behavior, rather than guaranteeing that every feature is already implemented.

## 2. Mechanical Arrangement

The mount is a **polar-aligned equatorial fork mount**.

A U-shaped fork supports the telescope between its two arms. The telescope pivots inside the fork around the declination axis. The entire fork rotates around the right ascension axis.

The two rotational axes are perpendicular:

- **RA axis:** rotates the complete fork assembly around an axis aligned with Earth’s rotational axis.
- **DEC axis:** rotates the telescope within the fork, changing its angular distance from the reference celestial pole.

The installation is in Mendoza, Argentina, at approximately latitude **−32.89°**. The RA axis therefore points toward the **south celestial pole**, approximately **32.89° above the southern horizon**.

Polar alignment describes the direction of the RA rotation axis. It does not imply that the telescope always points along that axis.

The fork geometry is intended to allow meridian crossing without a telescope–mount collision requiring a change of configuration. This does **not** imply unlimited mechanical travel.

## 3. Reference Position and Mechanical Coordinates

The firmware uses two absolute mechanical coordinates:

- `R`: RA-axis angle, in degrees.
- `D`: DEC-axis angle, in degrees.

These are angles of the **mount’s output axes**, after gearing. They are not motor-shaft angles.

The reference position, called **ZERO** or **HOME**, is:

    R = 0°
    D = 0°

At this position:

- The telescope points toward the reference celestial pole.
- For Mendoza, that pole is the south celestial pole.
- The fork has its defined zero rotational orientation around the polar axis.

Although RA is astronomically undefined at the exact celestial pole, the mechanical RA coordinate remains meaningful.

The physical zero must be established correctly. Moving to HOME and redefining the current position as ZERO are different operations: redefining ZERO changes the coordinate reference.

## 4. Positive Axis Directions

### RA

Positive `R` increases the telescope’s hour angle, moving its pointing westward around the polar axis.

Negative `R` moves eastward.

At the exact celestial pole, rotating RA changes the fork’s orientation but does not change the telescope’s ideal pointing direction.

### DEC

For the southern installation, starting at `R = 0°, D = 0°`, increasing `D` moves the telescope away from the south celestial pole toward the north along the local meridian.

| Mechanical position | Ideal pointing in Mendoza |
|---|---|
| `R = 0°, D = 0°` | South celestial pole |
| `R = 0°, D ≈ 57.11°` | Zenith |
| `R = 0°, D = 90°` | Celestial equator on the meridian |

At `R = 0°`, DEC motion follows a vertical great circle through the sky. At other RA positions, DEC follows the corresponding celestial meridian; it is **not generally a local vertical altitude adjustment**.

Positive DEC always retains the same mechanical direction. However, increasing mechanical `D` does not increase astronomical declination everywhere in the available travel range.

## 5. Absolute Coordinates and Absence of Meridian Flips

The mechanical coordinates are continuous, signed, absolute positions derived from motor-step counters.

The following principles are fundamental:

1. The same mechanical position, observing location and instant must produce the same reported celestial coordinates.
2. Pointing must not depend on the sequence of movements used to reach that position.
3. There is no remembered DEC orientation that changes the coordinate transformation.
4. Crossing the celestial meridian does not trigger a flip, a coordinate reset, or a reversal of either motor’s mechanical sign.
5. Mechanical positions and movement distances must not be wrapped modulo 180° or 360°.

The mount does not use “pier side” to decide how to point. Any compatibility field representing pier side must not control the geometry or introduce German-equatorial-mount behavior.

## 6. Mathematical Pointing Model

All trigonometric expressions below require consistent angular units. Mechanical angles are presented in degrees; implementations normally convert them to radians.

Let:

    λ = geographic latitude
    p = +1 when λ > 0°   (northern reference pole)
    p = −1 when λ ≤ 0°   (southern reference pole)

The latitude-zero convention is deliberately southern and must remain deterministic.

Define a celestial coordinate frame with:

- `X` toward hour angle zero on the celestial equator.
- `Y` toward hour angle +90° on the celestial equator.
- `Z` toward the north celestial pole.

The telescope’s ideal unit pointing vector is:

    x = sin(D) · cos(R)
    y = sin(D) · sin(R)
    z = p · cos(D)

Astronomical declination and hour angle are:

    δ = atan2(z, sqrt(x² + y²))
    H = atan2(y, x)

Right ascension is:

    α = normalize_to_24_hours(LST − H / 15)

Here `LST` is local sidereal time in hours, and `H` is in degrees.

**Mechanical RA angle and astronomical right ascension are different quantities.** A stationary mount has constant mechanical angles, but its reported astronomical right ascension changes as Earth rotates.

### Celestial Poles

At:

    D = 180° · n

for integer `n`, the telescope points at a celestial pole and astronomical RA is indeterminate.

A deterministic reporting convention is:

    α = normalize_to_24_hours(LST − R / 15)

This convention must not depend on movement history.

## 7. Converting a Celestial Target into a Mechanical Destination

For a requested astronomical position `(α, δ)`:

    H = 15 · (LST − α)
    c = 90° − p · δ

The mechanical position is one continuous, absolute coordinate `(R, D)`.  A
single pointing has many equivalent representations, generated by two
symmetries:

    flip:        (R, D)  →  (R + 180°, −D)
    full turn:   (R, D)  →  (R + 360°, D)     and     (R, D + 360°)

Concretely, every admissible destination is of the form

    R = H + 180° · f + 360° · k
    D = (1 − 2·f) · c + 360° · m

where `f ∈ {0, 1}` selects the flip and `k`, `m` are integers.  All of these
represent the same pointing direction.  They are not distinct modes, pier
sides, or a remembered orientation state: the mount has no flip state, and the
same physical position always yields the same reported coordinates.

A commanded GOTO chooses whichever representation is admissible and minimizes
travel.  That choice does not authorize an automatic flip while tracking.

Selection must:

- Respect configured mechanical limits.
- Apply the required horizon and trajectory restrictions.
- Account for the target’s motion during the estimated travel time.
- Validate the actual destination after conversion to motor steps.
- Prefer the smallest linear mechanical travel:

  cost = |R_target − R_current| + |D_target − D_current|

Ties are resolved by smaller RA travel, then smaller DEC travel, then a fixed deterministic ordering.

Flipped and periodic representations identify equivalent destinations; they do not authorize full revolutions or movement across a forbidden boundary.

For a target at a celestial pole, retain the current valid mechanical RA position and select an admissible DEC representation of the **requested pole**.

## 8. Tracking and Guiding

### Tracking

For ideal sidereal tracking:

- Mechanical RA increases continuously toward the west.
- Mechanical DEC remains constant.
- The RA rate is approximately **+0.004178° per second**.

No special action occurs when crossing the meridian.

Tracking must stop when further movement would violate an applicable limit. It must not flip, wrap the axis position, or select another equivalent pointing representation to continue.

### Pulse Guiding

Guiding directions describe corrections on the sky, not fixed motor directions in every configuration.

RA guide offsets relative to the base tracking rate are:

    East: negative RA offset
    West: positive RA offset

Away from a celestial pole, the mechanical DEC direction that increases astronomical declination is:

    north_direction = −p · sign(sin(D))

South uses the opposite direction.

This sign is derived from the current geometry. It is not a remembered orientation and does not change merely because the telescope crosses the meridian.

North/south guiding that reaches or crosses a celestial pole is rejected under the agreed guiding convention. Manual DEC movement may cross a pole if the other applicable limits permit it.

## 9. Mechanical and Horizon Limits

Two distinct restrictions apply.

### Mechanical Travel Limits

Configured minimum and maximum angles constrain each axis.

These limits remain authoritative for all movement types. The fork must not make unrestricted revolutions.

Previously discussed values such as RA `−90°…+90°` and DEC `−20°…+120°` were illustrative ranges, not universal geometric requirements or instructions to overwrite the configuration.

An external hardware end-stop is separate from firmware enforcement and must not be used as a substitute for software limits.

### Geometric Horizon

Let:

    φ = |λ|

The telescope’s geometric altitude satisfies:

    sin(altitude) =
        cos(φ) · sin(D) · cos(R)
      + sin(φ) · cos(D)

Normal observing motion must remain at or above the geometric horizon:

    altitude ≥ 0°

This is an ideal geometric horizon. It does not model terrain, buildings or atmospheric refraction.

A rectangular RA/DEC travel range alone cannot generally enforce this restriction: altitude depends on both axes together.

## 10. Movement Acceptance and Execution

A visible destination does not guarantee a valid route.

A GOTO or HOME command must be checked against both its destination and its intended mechanical movement. Rejection must not initiate a partial GOTO.

The agreed conservative approach checks the minimum altitude over the rectangle bounded by the initial and final mechanical coordinates. Because each axis moves monotonically during a normal slew, that rectangle contains all possible interleavings of the two axes.

This may reject some physically valid trajectories. That conservatism is intentional; the firmware is not required to search for detours.

Execution must also account for:

- The actual starting position when a queued command begins.
- Motor steps and RMT batches already committed.
- Simultaneous movement of both axes.
- Quantization and numeric precision.
- Commands queued to resume tracking after a slew.

Latitude must remain fixed while movement is active or pending, including standalone guiding.

If execution rejects a previously accepted movement, dependent queued actions must not continue as though the movement had succeeded.

## 11. Recovery from Below the Horizon

A mount initially below the horizon must remain recoverable.

Normal GOTO, HOME and tracking are rejected from that condition. Controlled manual recovery is allowed while respecting mechanical limits.

The agreed recovery model uses a frozen altitude floor equal to the altitude at the start of the manual movement. The committed movement must not descend below that floor.

This guarantees a minimum altitude; it does not require strictly monotonic ascent.

When transitioning back to normal operation:

1. Stop adding new batches.
2. Allow already committed batches to finish under the recovery rule.
3. Inspect the resulting position.
4. If it is above the horizon, establish the normal zero-altitude floor before issuing further batches.

The normal guarantee against returning below the horizon begins after this transition is completed.

## 12. Reference Principles for Future Fixes

Any future change must preserve:

- Absolute, history-independent mechanical coordinates.
- Fixed mechanical motor directions.
- No automatic meridian flip.
- No pier-side-dependent pointing.
- Correct separation between mechanical angles and celestial coordinates.
- Configurable mechanical limits and the geometric horizon restriction.
- Safe handling of queued and already committed movement.
- Explicit handling of celestial-pole singularities.
- Consistent conversion between output-axis angles and motor steps.

The mathematical model assumes correct polar alignment, perpendicular axes, a correctly established ZERO, and accurate transmission scaling.

Physical alignment errors, backlash, flexure and missed steps are separate effects; they must not be “corrected” by reintroducing orientation history or meridian-flip logic.