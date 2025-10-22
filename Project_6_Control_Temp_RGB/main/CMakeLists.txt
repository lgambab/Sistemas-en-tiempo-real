idf_component_register(SRCS "uart_commands.c" "rgb_led.c" "potentiometer.c" "button_control.c" "ntc_sensor.c" "main.c"
                       PRIV_REQUIRES spi_flash esp_adc esp_driver_ledc esp_timer esp_driver_uart
                       INCLUDE_DIRS "")
