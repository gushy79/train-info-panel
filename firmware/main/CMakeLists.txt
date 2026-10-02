file(GLOB_RECURSE SRC_FILES
    "app_main.c"
    "app/*.c"
    "display/*.c"
    "board/*.c"
    "net/*.c"
    "touch/*.c"
    "ui/*.c"
    "fixtures/*.c"
    "spike/*.c"
)

idf_component_register(SRCS ${SRC_FILES}
                        INCLUDE_DIRS "."
                        REQUIRES "esp_lcd" "driver" "esp_lcd_jd9853"
                                 "esp_wifi" "esp_event" "esp_netif" "esp_http_client" "esp-tls"
                                 "nvs_flash" "json" "lwip" "esp_timer")
