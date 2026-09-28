package main

import (
	"html/template"
	"log"
	"net/http"
	"strconv"
	"strings"
)

type App struct {
	store *MemoryStore
	mqtt  *MQTTBridge
	tmpl  *template.Template
}

func NewApp(store *MemoryStore, mqttBridge *MQTTBridge, tmpl *template.Template) *App {
	return &App{
		store: store,
		mqtt:  mqttBridge,
		tmpl:  tmpl,
	}
}

func (a *App) render(w http.ResponseWriter, name string, data any) {
	err := a.tmpl.ExecuteTemplate(w, name, data)
	if err != nil {
		http.Error(w, "Internal Server Error", http.StatusInternalServerError)
		log.Println("template error:", err)
	}
}

func normalizeCodeInput(code string) string {
	code = strings.TrimSpace(code)
	if code == "" {
		return ""
	}

	n, err := strconv.Atoi(code)
	if err != nil || n < 0 || n > 99 {
		return ""
	}

	return strconv.Itoa(n)
}

func displayCode(code string) string {
	n, err := strconv.Atoi(strings.TrimSpace(code))
	if err != nil {
		return code
	}
	return strconv.FormatInt(int64(n), 10)
}

func buildMissingGroups(items []GroceryItem) []CategoryGroup {
	groupsMap := map[string][]GroceryItem{
		"Pantry":   {},
		"Fridge":   {},
		"Cleaning": {},
	}

	for _, item := range items {
		groupsMap[item.Category] = append(groupsMap[item.Category], item)
	}

	all := []CategoryGroup{
		{Name: "Pantry", Items: groupsMap["Pantry"]},
		{Name: "Fridge", Items: groupsMap["Fridge"]},
		{Name: "Cleaning", Items: groupsMap["Cleaning"]},
	}

	var result []CategoryGroup
	for _, g := range all {
		if len(g.Items) > 0 {
			result = append(result, g)
		}
	}

	return result
}

func catalogMessage(code string) string {
	switch code {
	case "duplicate_id":
		return "This ID is already used."
	case "invalid_input":
	return "Please enter a valid item name and category."
	case "deleted":
		return "Item deleted."
		case "synced":
	return "Catalog synced to ESP32."
	case "catalog_full":
	return "Catalog is full. No more IDs are available."
	case "missing_name":
	return "Please enter an item name."
case "missing_category":
	return "Please select a category."
	default:
		return ""
	}
}

func (a *App) homeHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	missing, active, archived := a.store.Counts()

	data := HomePageData{
		Title:         "Grocery Tracker",
		MissingCount:  missing,
		ActiveCount:   active,
		ArchivedCount: archived,
		MQTTEnabled:   a.mqtt.Enabled(),
		MQTTConnected: a.mqtt.Connected(),
	}

	a.render(w, "home.html", data)
}

func (a *App) listHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/list" {
		http.NotFound(w, r)
		return
	}

	missingItems := a.store.MissingItems()

	data := ListPageData{
		Title:          "Shopping List",
		CategoryGroups: buildMissingGroups(missingItems),
		MQTTEnabled:    a.mqtt.Enabled(),
		MQTTConnected:  a.mqtt.Connected(),
	}

	a.render(w, "list.html", data)
}

func (a *App) catalogHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/catalog" {
		http.NotFound(w, r)
		return
	}

	data := CatalogPageData{
		Title:         "Catalog",
		Items:         a.store.ActiveItems(),
		Error:         catalogMessage(r.URL.Query().Get("msg")),
		MQTTEnabled:   a.mqtt.Enabled(),
		MQTTConnected: a.mqtt.Connected(),
	}

	a.render(w, "catalog.html", data)
}

func (a *App) addToShoppingListHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	id := strings.TrimSpace(r.FormValue("id"))
	if id == "" {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	if err := a.store.SetItemState(id, "MISSING"); err != nil {
		log.Println("addToShoppingListHandler:", err)
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	if err := a.mqtt.PublishState(id, "MISSING"); err != nil {
		log.Println("mqtt publish state error:", err)
	}

	http.Redirect(w, r, "/list", http.StatusSeeOther)
}

func (a *App) markBoughtHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Redirect(w, r, "/list", http.StatusSeeOther)
		return
	}

	id := strings.TrimSpace(r.FormValue("id"))
	if id == "" {
		http.Redirect(w, r, "/list", http.StatusSeeOther)
		return
	}

	if err := a.store.SetItemState(id, "HAVE"); err != nil {
		log.Println("markBoughtHandler:", err)
		http.Redirect(w, r, "/list", http.StatusSeeOther)
		return
	}

	if err := a.mqtt.PublishState(id, "HAVE"); err != nil {
		log.Println("mqtt publish state error:", err)
	}

	http.Redirect(w, r, "/list", http.StatusSeeOther)
}

func (a *App) upsertCatalogItemHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	name := strings.TrimSpace(r.FormValue("name"))
	category := strings.TrimSpace(r.FormValue("category"))

	if name == "" {
	http.Redirect(w, r, "/catalog?msg=missing_name", http.StatusSeeOther)
	return
}

switch category {
case "Pantry", "Fridge", "Cleaning":
default:
	http.Redirect(w, r, "/catalog?msg=missing_category", http.StatusSeeOther)
	return
}
	nextID, err := a.store.NextAvailableID()
	if err != nil {
		http.Redirect(w, r, "/catalog?msg=catalog_full", http.StatusSeeOther)
		return
	}

	item := GroceryItem{
		ID:       nextID,
		Code:     formatTwoDigitCode(nextID),
		Name:     name,
		Category: category,
		State:    "HAVE",
		Archived: false,
	}

	if err := a.store.UpsertItem(item); err != nil {
		log.Println("upsertCatalogItemHandler:", err)
		http.Redirect(w, r, "/catalog?msg=invalid_input", http.StatusSeeOther)
		return
	}

	if err := saveCatalogToFile(a.store.CatalogItemsForSave()); err != nil {
		log.Println("save catalog file error:", err)
	}

	if saved, ok := a.store.GetItem(item.ID); ok {
		if err := a.mqtt.PublishCatalogSet(saved); err != nil {
			log.Println("mqtt publish catalog set error:", err)
		}
	}

	http.Redirect(w, r, "/catalog", http.StatusSeeOther)
}
func (a *App) deleteCatalogItemHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	id := strings.TrimSpace(r.FormValue("id"))
	if id == "" {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}
	_ = a.store.SetItemState(id, "HAVE")
_ = a.mqtt.PublishState(id, "HAVE")

	if err := a.store.DeleteItem(id); err != nil {
		log.Println("deleteCatalogItemHandler:", err)
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	if err := saveCatalogToFile(a.store.CatalogItemsForSave()); err != nil {
		log.Println("save catalog file error:", err)
	}

	if err := a.mqtt.PublishCatalogDelete(id, true); err != nil {
		log.Println("mqtt publish catalog delete error:", err)
	}

	http.Redirect(w, r, "/catalog?msg=deleted", http.StatusSeeOther)
}
func (a *App) downloadListHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/list/download" {
		http.NotFound(w, r)
		return
	}

	missingItems := a.store.MissingItems()
	groups := buildMissingGroups(missingItems)

	var b strings.Builder
	b.WriteString("Grocery List\n\n")

	for _, group := range groups {
		b.WriteString(group.Name)
		b.WriteString("\n")
		for _, item := range group.Items {
			b.WriteString("- ")
			b.WriteString(item.Name)
			b.WriteString("\n")
		}
		b.WriteString("\n")
	}

	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Content-Disposition", `attachment; filename="grocery-list.txt"`)
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(b.String()))
}
func (a *App) syncCatalogHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Redirect(w, r, "/catalog", http.StatusSeeOther)
		return
	}

	a.mqtt.SyncCatalogFull(a.store.CatalogItemsForSave())
	http.Redirect(w, r, "/catalog?msg=synced", http.StatusSeeOther)
}