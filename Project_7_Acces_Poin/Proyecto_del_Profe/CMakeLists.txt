idf_component_register(SRCS main.c rgb_led.c wifi_app.c
                    PRIV_REQUIRES esp_wifi nvs_flash driver
                    INCLUDE_DIRS ".")
