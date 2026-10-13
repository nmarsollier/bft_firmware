"""Independent oracle — geometry and astronomy without reusing the firmware.

Spherical angular distance between equatorial coordinates, handling the RA
24 h / 0 h wrap and careful with polar singularities. RA in hours; DEC in degrees.

Also the slew-time model: a closed-form replica of the firmware ramp profile
(main/motors/motors_motion_task.c) so a test can predict how long a goto must
take from the observed axis distance.
"""
import math


# ── slew ramp constants (mirror motors_motion_task.c) ─────────────
MIN_SLEW_DPS = 0.8        # MIN_SLEW_CDS / 100
ACCEL_DIST_DEG = 5.0      # ACCEL_DIST_CDS / 100
DECEL_DIST_DEG = 5.0      # DECEL_DIST_CDS / 100
SHORT_SLEW_DEG = 2.0      # SHORT_SLEW_CDS / 100
GENTLE_SLEW_DEG = 8.0     # GENTLE_SLEW_CDS / 100


def capped_speed(speed_dps, distance_deg):
    """Target speed after the gentle-slew cap (motors ramp_velocity)."""
    if distance_deg < SHORT_SLEW_DEG:
        return MIN_SLEW_DPS
    if distance_deg < GENTLE_SLEW_DEG:
        return min(speed_dps, MIN_SLEW_DPS * 4.0)  # 3.2 dps cap
    return speed_dps


def slew_time(distance_deg, speed_dps):
    """Exact single-axis slew duration (s) under the firmware ramp.

    Velocity ramps linearly with *distance* from MIN_SLEW_DPS up to the capped
    target and back down to MIN_SLEW_DPS — a trapezoid, or a triangle when the
    move is shorter than the accel+decel window.  Below SHORT_SLEW_DEG the axis
    runs at constant MIN_SLEW_DPS.  dt = ∫ dx / v(x) has the closed form below.
    """
    if distance_deg <= 0.0:
        return 0.0
    capped = capped_speed(speed_dps, distance_deg)
    if distance_deg < SHORT_SLEW_DEG:
        return distance_deg / MIN_SLEW_DPS
    ramp_ln = math.log(capped / MIN_SLEW_DPS) / (capped - MIN_SLEW_DPS)
    if distance_deg <= ACCEL_DIST_DEG + DECEL_DIST_DEG:  # triangular
        return distance_deg * ramp_ln
    # trapezoidal: accel + cruise + decel
    t_ramp = ACCEL_DIST_DEG * ramp_ln
    return 2.0 * t_ramp + (distance_deg - ACCEL_DIST_DEG - DECEL_DIST_DEG) / capped


def goto_time(ra_dist_deg, dec_dist_deg, speed_dps):
    """Duration of a two-axis slew: the slower axis (larger distance) wins."""
    return max(slew_time(ra_dist_deg, speed_dps),
               slew_time(dec_dist_deg, speed_dps))


def angular_separation_deg(ra1_h, dec1_deg, ra2_h, dec2_deg):
    """Spherical angular distance (degrees) between two equatorial positions.

    Uses unit vectors to be correct near the pole, where a direct RA
    subtraction loses meaning.
    """
    ra1 = math.radians((ra1_h % 24.0) * 15.0)
    ra2 = math.radians((ra2_h % 24.0) * 15.0)
    d1 = math.radians(dec1_deg)
    d2 = math.radians(dec2_deg)

    x1 = math.cos(d1) * math.cos(ra1)
    y1 = math.cos(d1) * math.sin(ra1)
    z1 = math.sin(d1)
    x2 = math.cos(d2) * math.cos(ra2)
    y2 = math.cos(d2) * math.sin(ra2)
    z2 = math.sin(d2)

    dot = x1 * x2 + y1 * y2 + z1 * z2
    dot = max(-1.0, min(1.0, dot))
    return math.degrees(math.acos(dot))


def normalize_hours(h):
    """Normaliza RA a [0, 24)."""
    return h % 24.0


def wrap_ha(h):
    """Wrap an hour angle to [-12, +12)."""
    h = h % 24.0
    if h >= 12.0:
        h -= 24.0
    return h
