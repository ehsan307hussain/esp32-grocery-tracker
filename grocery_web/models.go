package main

type GroceryItem struct {
	ID       string
	Code     string
	Name     string
	Category string
	State    string // "MISSING" or "HAVE"
	Archived bool
}

type CategoryGroup struct {
	Name  string
	Items []GroceryItem
}

type HomePageData struct {
	Title         string
	MissingCount  int
	ActiveCount   int
	ArchivedCount int
	MQTTEnabled   bool
	MQTTConnected bool
}

type ListPageData struct {
	Title          string
	CategoryGroups []CategoryGroup
	MQTTEnabled    bool
	MQTTConnected  bool
}

type CatalogPageData struct {
	Title         string
	Items         []GroceryItem
	Error         string
	MQTTEnabled   bool
	MQTTConnected bool
}