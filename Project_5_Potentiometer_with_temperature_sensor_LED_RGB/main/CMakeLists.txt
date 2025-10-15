idf_component_register(SRCS "main.c" "potentiometer.c" "rgb_led.c" "ntc_temp.c"
                       PRIV_REQUIRES spi_flash esp_adc esp_driver_ledc
                       INCLUDE_DIRS "")
