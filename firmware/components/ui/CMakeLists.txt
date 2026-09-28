idf_component_register(SRCS "ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES display catalog list_store storage_nvs mqtt_mgr)
