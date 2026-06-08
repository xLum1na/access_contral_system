idf_component_register(
    SRCS
        "pal_log/pal_log.c"
        "pal_ledc/pal_ledc.c"
        "pal_i2c/pal_i2c.c"
        "pal_gpio/pal_gpio.c"
        "pal_uart/pal_uart.c"
        "pal_spi/pal_spi.c"
        "pal_mipi_csi/pal_mipi_csi.c"
        "pal_mipi_dsi/pal_mipi_dsi.c"

    INCLUDE_DIRS
        "pal_log/include"
        "pal_ledc/include"
        "pal_i2c/include"
        "pal_gpio/include"
        "pal_uart/include"
        "pal_spi/include"
        "pal_mipi_csi/include"
        "pal_mipi_dsi/include"
        "${IDF_PATH}/components/esp_lcd/dsi"

    REQUIRES
        driver
        esp_timer
        esp_driver_cam
        esp_lcd)
