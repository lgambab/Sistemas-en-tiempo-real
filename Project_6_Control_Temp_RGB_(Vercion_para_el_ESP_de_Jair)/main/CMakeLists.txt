idf_component_register(SRCS "main_u.c" "button_driver.c" "ntc_driver.c" "pot_driver.c" "rgb_led_driver.c" 
                       REQUIRES esp_driver_uart esp_driver_gpio esp_driver_ledc esp_adc
                       INCLUDE_DIRS "")
