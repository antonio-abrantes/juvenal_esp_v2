idf_component_register(
    SRCS "juvenal_main.cpp" "juvenal_face_neutral.c" "juvenal_web.cpp"
         "juvenal_ws.cpp" "juvenal_speaker.cpp" "juvenal_led.c"
    INCLUDE_DIRS "."
    EMBED_FILES "juvehnal.tflite"
    EMBED_TXTFILES "juvenal_web.html"
    REQUIRES esp_driver_i2s led_strip micro_wake_word_idf esp_wifi esp_event
             esp_netif nvs_flash esp_http_client esp_http_server json mbedtls
             esp_lcd esp_driver_spi esp_timer
             espressif__esp_lcd_gc9a01 espressif__esp_lvgl_port lvgl__lvgl
             espressif__esp_websocket_client
)


