idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES keypad display catalog list_store ui nvs_flash storage_nvs wifi_mgr mqtt_mgr)
