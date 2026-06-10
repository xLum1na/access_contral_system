idf_component_register(
    SRCS
        "dal_touch/dal_touch.c"
        "dal_touch/ft5406.c"
        "dal_storage/dal_storage.c"
        "dal_relay/dal_relay.c"
        "dal_network/dal_network.c"
        "dal_display/dal_display.c"
        "dal_display/attiny88.c"
        "dal_display/tc358762.c"
        "dal_audio/es8311.c"
        "dal_audio/dal_audio.c"

    INCLUDE_DIRS
        "dal_touch/include"
        "dal_storage/include"
        "dal_relay/include"
        "dal_network/include"
        "dal_display/include"
        "dal_audio/include"

    REQUIRES
        pal osal esp_eth esp_netif fatfs sdmmc)
