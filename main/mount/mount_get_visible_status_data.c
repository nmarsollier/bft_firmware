/* Mount - mount_get_visible_status_data.c
 *
 * Purpose: build the visible status snapshot for UI and API consumers.
 */
#include "mount.h"
#include "mount_internal.h"

#include "motors.h"

VisibleStatusData mount_get_visible_status(void) {
    MotorsState pos = motors_current_state();
    AxisCoordinates axis = {
        .ra_axis_deg = motors_get_ra_deg(),
        .dec_axis_deg = motors_get_dec_deg()
    };

    EquatorialCoordinates eq = axis_to_equatorial(axis);

    return (VisibleStatusData)
    {
        .status = pos.status,
        .tracking = pos.tracking,
        .ra = ra_hours_to_hms(eq.ra_hours),
        .dec = dec_deg_to_dms(eq.dec_deg),
        .lst_hours = mount_get_lst(),
        .settings = mount_internal_state,
    };
}
