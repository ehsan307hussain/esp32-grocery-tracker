idf_component_register(
  SRCS "mqtt_mgr.c"
  INCLUDE_DIRS "."
  REQUIRES mqtt esp_event  json catalog list_store log     secrets

)
target_add_binary_data(${COMPONENT_LIB} "certs/hivemq_ca.pem" TEXT)
