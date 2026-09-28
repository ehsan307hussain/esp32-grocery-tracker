package main

import (
	"os"
)

type Config struct {
	HTTPAddr     string
	MQTTEnabled  bool
	MQTTBroker   string
	MQTTUsername string
	MQTTPassword string
	MQTTClientID string
	BaseTopic    string
}

func getEnv(key, fallback string) string {
	v := os.Getenv(key)
	if v == "" {
		return fallback
	}
	return v
}

func LoadConfig() Config {
	broker := os.Getenv("MQTT_BROKER")
	username := os.Getenv("MQTT_USERNAME")
	password := os.Getenv("MQTT_PASSWORD")

	enabled := broker != "" && username != "" && password != ""

	return Config{
		HTTPAddr:     getEnv("HTTP_ADDR", ":8080"),
		MQTTEnabled:  enabled,
		MQTTBroker:   broker,
		MQTTUsername: username,
		MQTTPassword: password,
		MQTTClientID: getEnv("MQTT_CLIENT_ID", "grocery-web"),
		BaseTopic:    getEnv("MQTT_BASE_TOPIC", "home/grocery"),
	}
}
