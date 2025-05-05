idf_component_register(
    SRCS "main.c"
    "lvgl_port.c"
    "waveshare_rgb_lcd_port.c"
    "wifi_manager.c"
    "power_monitor.c"
    "settings_ui.c"
    INCLUDE_DIRS ".")

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
