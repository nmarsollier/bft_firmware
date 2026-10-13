# CDC-ACM protocol sources — compiled when the TinyUSB CDC class is enabled.
set(PROTO_CDC_SRCS "")
if(CONFIG_TINYUSB_CDC_ENABLED)
        set(PROTO_CDC_SRCS
        "usb_cdc/usb_cdc_init.c"
        "usb_cdc/usb_cdc_rx.c"
        "usb_cdc/usb_cdc_tx.c"

        "proto/proto_init.c"
        "proto/proto_frame.c"
        "proto/proto_json.c"
        "proto/proto_dispatch.c"
        "proto/proto_session.c"
        "proto/proto_capabilities.c"
        "proto/proto_state.c"
        "proto/proto_config.c"
        "proto/proto_control.c"
        "proto/proto_action.c"
        )
endif()

idf_component_register(
        SRCS
        "main.c"

        "power/power.c"

        "led/led_init.c"
        "led/led_set_state.c"
        "led/led_update.c"
        "led/led_beacon.c"

        "buzzer/buzzer_init.c"
        "buzzer/buzzer_play.c"
        "buzzer/buzzer_update.c"

        "motors/motors_current_state.c"
        "motors/motors_enter_error_state.c"
        "motors/motors_get_slewing_speed.c"
        "motors/motors_get_tracking_speed.c"
        "motors/motors_home.c"
        "motors/motors_init.c"
        "motors/motors_move_axis_speed.c"
        "motors/motors_park.c"
        "motors/motors_unpark.c"
        "motors/motors_slew_axis.c"
        "motors/motors_slew_to_angle.c"
        "motors/motors_start_tracking.c"
        "motors/motors_stop.c"
        "motors/motors_tools.c"

        "motors/motors_hw.c"
        "motors/motors_limits.c"
        "motors/motors_rmt.c"
        "motors/motors_command_queue.c"
        "motors/motors_motion_task.c"
        "motors/motors_pulse_guide.c"
        "motors/motors_is_pulse_guiding.c"
        "motors/motors_power.c"

        "mount/mount_init.c"
        "mount/mount_coordinate_tools.c"
        "mount/mount_slew_to_coordinates.c"
        "mount/mount_get_lst.c"
        "mount/mount_home.c"
        "mount/mount_limits.c"
        "mount/mount_move_axis.c"
        "mount/mount_set_move_axis_speed.c"
        "mount/mount_park.c"
        "mount/mount_settings_update.c"
        "mount/mount_settings_load.c"
        "mount/mount_settings_save.c"
        "mount/mount_stop.c"
        "mount/mount_set_system_time.c"
        "mount/mount_tools.c"
        "mount/mount_set_tracking.c"
        "mount/mount_unpark.c"
        "mount/mount_get_visible_status_data.c"
        "mount/mount_pulse_guide.c"

        "runtime/runtime_loop.c"
        "runtime/runtime_setup.c"

        "utils/number_utils.c"
        "utils/string_utils.c"

        ${PROTO_CDC_SRCS}

        INCLUDE_DIRS
        "."
        "buzzer"
        "config"
        "led"
        "mount"
        "motors"
        "power"
        "runtime"
        "utils"
        "usb_cdc"
        "proto"

        REQUIRES
        esp_timer
        nvs_flash
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_rmt
        esp_adc
)
