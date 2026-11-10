idf_component_register(
    SRCS "juvenal_main.cpp" "juvenal_led.c"
    INCLUDE_DIRS "."
    EMBED_FILES "juvehnal.tflite"
    REQUIRES esp_driver_i2s led_strip micro_wake_word_idf
)


