#!/usr/bin/env bash

export MQTT_BROKER='ssl://5b03d33a134c4eb9b36309bf9052b9a8.s1.eu.hivemq.cloud:8883'
export MQTT_USERNAME='ehsan'
export MQTT_PASSWORD='Sunasuna307@'
export MQTT_CLIENT_ID='grocery-web'
export MQTT_BASE_TOPIC='home/grocery'
export HTTP_ADDR=':8080'

go run .