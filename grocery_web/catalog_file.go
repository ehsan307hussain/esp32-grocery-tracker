package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const catalogFilePath = "data/catalog.json"

func ensureDataDir() error {
	return os.MkdirAll("data", 0o755)
}

func saveCatalogToFile(items []GroceryItem) error {
	if err := ensureDataDir(); err != nil {
		return err
	}

	b, err := json.MarshalIndent(items, "", "  ")
	if err != nil {
		return err
	}

	tmpPath := filepath.Join("data", "catalog.json.tmp")
	if err := os.WriteFile(tmpPath, b, 0o644); err != nil {
		return err
	}

	return os.Rename(tmpPath, catalogFilePath)
}

func loadCatalogFromFile() ([]GroceryItem, error) {
	b, err := os.ReadFile(catalogFilePath)
	if err != nil {
		if os.IsNotExist(err) {
			return []GroceryItem{}, nil
		}
		return nil, err
	}

	var items []GroceryItem
	if err := json.Unmarshal(b, &items); err != nil {
		return nil, err
	}

	return items, nil
}