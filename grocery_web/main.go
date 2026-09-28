package main

import (
	"html/template"
	"log"
	"net/http"
)

func main() {
	cfg := LoadConfig()

	store := NewMemoryStore()

	loadedItems, err := loadCatalogFromFile()
	if err != nil {
		log.Println("load catalog file error:", err)
	} else {
		if err := store.LoadCatalogItems(loadedItems); err != nil {
			log.Println("store load catalog error:", err)
		}
	}

	mqttBridge := NewMQTTBridge(cfg, store)

	if err := mqttBridge.Connect(); err != nil {
		log.Println("MQTT connect failed:", err)
	} else {
		mqttBridge.SyncCatalogFull(store.CatalogItemsForSave())
	}

	tmpl := template.Must(template.ParseGlob("templates/*.html"))
	app := NewApp(store, mqttBridge, tmpl)

	mux := http.NewServeMux()

	fileServer := http.FileServer(http.Dir("static"))
	mux.Handle("/static/", http.StripPrefix("/static/", fileServer))

	mux.HandleFunc("/", app.homeHandler)
	mux.HandleFunc("/list", app.listHandler)
	mux.HandleFunc("/catalog", app.catalogHandler)

	mux.HandleFunc("/actions/list/add", app.addToShoppingListHandler)
	mux.HandleFunc("/actions/list/have", app.markBoughtHandler)
	mux.HandleFunc("/actions/catalog/upsert", app.upsertCatalogItemHandler)
	mux.HandleFunc("/actions/catalog/delete", app.deleteCatalogItemHandler)
    mux.HandleFunc("/actions/catalog/sync", app.syncCatalogHandler)
	mux.HandleFunc("/list/download", app.downloadListHandler)

	log.Println("Starting server on http://localhost" + cfg.HTTPAddr)

	err = http.ListenAndServe(cfg.HTTPAddr, mux)
	if err != nil {
		log.Fatal(err)
	}
}