idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver dal esp_video esp_driver_jpeg esp_psram esp_timer esp_cam_sensor esp_sccb_intf)
