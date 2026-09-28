package main

import (
	"crypto/tls"
	"encoding/json"
	"fmt"
	"log"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

type CatalogSetPayload struct {
	ID       int    `json:"id"`
	Code     int    `json:"code"`
	Name     string `json:"name"`
	Category string `json:"category"`
	Archived bool   `json:"archived"`
}


type CatalogDeletePayload struct {
	ID        int  `json:"id"`
	ClearList bool `json:"clear_list"`
}

type MQTTBridge struct {
	cfg       Config
	store     *MemoryStore
	client    mqtt.Client
	connected atomic.Bool
}

func NewMQTTBridge(cfg Config, store *MemoryStore) *MQTTBridge {
	return &MQTTBridge{
		cfg:   cfg,
		store: store,
	}
}

func (m *MQTTBridge) Enabled() bool {
	return m.cfg.MQTTEnabled
}

func (m *MQTTBridge) Connected() bool {
	if !m.cfg.MQTTEnabled {
		return false
	}
	return m.connected.Load()
}

func (m *MQTTBridge) Connect() error {
	if !m.cfg.MQTTEnabled {
		log.Println("MQTT disabled: missing MQTT_BROKER / MQTT_USERNAME / MQTT_PASSWORD")
		return nil
	}

	opts := mqtt.NewClientOptions()
	opts.AddBroker(m.cfg.MQTTBroker)
	opts.SetClientID(m.cfg.MQTTClientID)
	opts.SetUsername(m.cfg.MQTTUsername)
	opts.SetPassword(m.cfg.MQTTPassword)
	opts.SetTLSConfig(&tls.Config{
		MinVersion: tls.VersionTLS12,
	})
	opts.SetAutoReconnect(true)
	opts.SetConnectRetry(true)
	opts.SetConnectRetryInterval(5 * time.Second)

	opts.SetOnConnectHandler(func(c mqtt.Client) {
		log.Println("MQTT connected")
		m.connected.Store(true)

		stateTopic := fmt.Sprintf("%s/state/+", m.cfg.BaseTopic)
		catalogSetTopic := fmt.Sprintf("%s/catalog/set", m.cfg.BaseTopic)
		catalogDeleteTopic := fmt.Sprintf("%s/catalog/delete", m.cfg.BaseTopic)

		if token := c.Subscribe(stateTopic, 1, m.handleStateMessage); token.Wait() && token.Error() != nil {
			log.Println("MQTT subscribe error (state):", token.Error())
			return
		}

		if token := c.Subscribe(catalogSetTopic, 1, m.handleCatalogSetMessage); token.Wait() && token.Error() != nil {
			log.Println("MQTT subscribe error (catalog/set):", token.Error())
			return
		}

		if token := c.Subscribe(catalogDeleteTopic, 1, m.handleCatalogDeleteMessage); token.Wait() && token.Error() != nil {
			log.Println("MQTT subscribe error (catalog/delete):", token.Error())
			return
		}

		log.Println("Subscribed to", stateTopic)
		log.Println("Subscribed to", catalogSetTopic)
		log.Println("Subscribed to", catalogDeleteTopic)
	})

	opts.SetConnectionLostHandler(func(c mqtt.Client, err error) {
		log.Println("MQTT connection lost:", err)
		m.connected.Store(false)
	})

	m.client = mqtt.NewClient(opts)
	token := m.client.Connect()
	if token.Wait() && token.Error() != nil {
		return token.Error()
	}
	return nil
}

func (m *MQTTBridge) handleStateMessage(client mqtt.Client, msg mqtt.Message) {
	topic := msg.Topic()
	payload := strings.TrimSpace(string(msg.Payload()))

	prefix := m.cfg.BaseTopic + "/state/"
	if !strings.HasPrefix(topic, prefix) {
		return
	}

	id := strings.TrimPrefix(topic, prefix)
	if id == "" {
		return
	}

	log.Printf("MQTT state update: id=%s payload=%s\n", id, payload)
	m.store.SetStateFromMQTT(id, payload)
}

func (m *MQTTBridge) handleCatalogSetMessage(client mqtt.Client, msg mqtt.Message) {
	payloadText := strings.TrimSpace(string(msg.Payload()))
	if payloadText == "" {
		return
	}

	var payload CatalogSetPayload
	if err := json.Unmarshal(msg.Payload(), &payload); err != nil {
		log.Println("MQTT catalog/set JSON error:", err)
		return
	}

	id := strconv.Itoa(payload.ID)
	code := strconv.Itoa(payload.Code)
	if payload.Name == "" {
		log.Println("MQTT catalog/set ignored: empty name")
		return
	}

	item := GroceryItem{
		ID:       id,
		Code:     code,
		Name:     strings.TrimSpace(payload.Name),
		Category: strings.TrimSpace(payload.Category),
		State:    "HAVE",
		Archived: payload.Archived,
	}

	// Preserve existing state if item already exists.
	if existing, ok := m.store.GetItem(item.ID); ok {
		item.State = existing.State
	}

	if err := m.store.UpsertItem(item); err != nil {
		log.Println("MQTT catalog/set store error:", err)
		return
	}

	log.Printf("MQTT catalog/set applied: id=%s name=%s category=%s\n", item.ID, item.Name, item.Category)
}

func (m *MQTTBridge) handleCatalogDeleteMessage(client mqtt.Client, msg mqtt.Message) {
	payloadText := strings.TrimSpace(string(msg.Payload()))
	if payloadText == "" {
		return
	}

	var payload CatalogDeletePayload
	if err := json.Unmarshal(msg.Payload(), &payload); err != nil {
		log.Println("MQTT catalog/delete JSON error:", err)
		return
	}

	id := strconv.Itoa(payload.ID)

	if payload.ClearList {
		_ = m.store.SetItemState(id, "HAVE")
	}

	m.store.DeleteItemIfExists(id)

	log.Printf("MQTT catalog/delete applied: id=%s clear_list=%v\n", id, payload.ClearList)
}

func (m *MQTTBridge) PublishState(id, state string) error {
	if !m.cfg.MQTTEnabled || m.client == nil || !m.client.IsConnected() {
		return nil
	}

	topic := fmt.Sprintf("%s/state/%s", m.cfg.BaseTopic, id)
	token := m.client.Publish(topic, 1, true, normalizeState(state))
	token.Wait()
	return token.Error()
}

func (m *MQTTBridge) PublishCatalogSet(item GroceryItem) error {
	if !m.cfg.MQTTEnabled || m.client == nil || !m.client.IsConnected() {
		return nil
	}

	idInt, err := strconv.Atoi(strings.TrimSpace(item.ID))
	if err != nil {
		return err
	}

	codeInt, err := strconv.Atoi(strings.TrimSpace(item.Code))
	if err != nil {
		return err
	}

	payload := CatalogSetPayload{
		ID:       idInt,
		Code:     codeInt,
		Name:     item.Name,
		Category: item.Category,
		Archived: item.Archived,
	}

	b, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	topic := fmt.Sprintf("%s/catalog/set", m.cfg.BaseTopic)
	token := m.client.Publish(topic, 1, false, b)
	token.Wait()
	return token.Error()
}

func (m *MQTTBridge) PublishCatalogDelete(id string, clearList bool) error {
	if !m.cfg.MQTTEnabled || m.client == nil || !m.client.IsConnected() {
		return nil
	}

	idInt, err := strconv.Atoi(strings.TrimSpace(id))
	if err != nil {
		return err
	}

	payload := CatalogDeletePayload{
		ID:        idInt,
		ClearList: clearList,
	}

	b, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	topic := fmt.Sprintf("%s/catalog/delete", m.cfg.BaseTopic)
	token := m.client.Publish(topic, 1, false, b)
	token.Wait()
	return token.Error()
}
func (m *MQTTBridge) ReplayCatalog(items []GroceryItem) {
	if !m.cfg.MQTTEnabled || m.client == nil || !m.client.IsConnected() {
		log.Println("ReplayCatalog skipped: MQTT not connected")
		return
	}

	for _, item := range items {
		if item.Name == "" || item.ID == "" {
			continue
		}

		if err := m.PublishCatalogSet(item); err != nil {
			log.Println("ReplayCatalog publish error:", err)
		} else {
			log.Printf("ReplayCatalog published: id=%s name=%s\n", item.ID, item.Name)
		}
	}
}
func (m *MQTTBridge) SyncCatalogFull(items []GroceryItem) {
	if !m.cfg.MQTTEnabled || m.client == nil || !m.client.IsConnected() {
		log.Println("SyncCatalogFull skipped: MQTT not connected")
		return
	}


	// Step 2: re-seed with the current Go catalog.
	for _, item := range items {
		if item.Name == "" || item.ID == "" {
			continue
		}

		if err := m.PublishCatalogSet(item); err != nil {
			log.Println("SyncCatalogFull set error:", err)
		} else {
			log.Printf("SyncCatalogFull set: id=%s name=%s\n", item.ID, item.Name)
		}
	}
}