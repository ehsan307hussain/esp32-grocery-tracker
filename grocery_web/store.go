package main

import (
	"errors"
	"fmt"
	"sort"
	"strconv"
	"strings"
	"sync"
)

type MemoryStore struct {
	mu    sync.RWMutex
	items map[string]GroceryItem
}

func NewMemoryStore() *MemoryStore {
	return &MemoryStore{
		items: make(map[string]GroceryItem),
	}
}

func normalizeState(v string) string {
	v = strings.ToUpper(strings.TrimSpace(v))
	if v == "MISSING" {
		return "MISSING"
	}
	return "HAVE"
}

func itemNumericID(item GroceryItem) int {
	if n, err := strconv.Atoi(strings.TrimSpace(item.ID)); err == nil {
		return n
	}
	if n, err := strconv.Atoi(strings.TrimSpace(item.Code)); err == nil {
		return n
	}
	return 999999
}

func (s *MemoryStore) LoadCatalogItems(items []GroceryItem) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.items = make(map[string]GroceryItem)

	for _, item := range items {
		item.ID = strings.TrimSpace(item.ID)
		item.Code = strings.TrimSpace(item.Code)
		item.Name = strings.TrimSpace(item.Name)
		item.Category = strings.TrimSpace(item.Category)
		item.State = normalizeState(item.State)

		if item.ID == "" || item.Name == "" {
			continue
		}
		if item.Code == "" {
			item.Code = item.ID
		}
		if item.Category == "" {
			item.Category = "Pantry"
		}

		s.items[item.ID] = item
	}

	return nil
}

func (s *MemoryStore) CatalogItemsForSave() []GroceryItem {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var result []GroceryItem
	for _, item := range s.items {
		if !item.Archived {
			result = append(result, item)
		}
	}

	sort.Slice(result, func(i, j int) bool {
		return itemNumericID(result[i]) < itemNumericID(result[j])
	})

	return result
}

func (s *MemoryStore) UpsertItem(item GroceryItem) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	item.ID = strings.TrimSpace(item.ID)
	item.Code = strings.TrimSpace(item.Code)
	item.Name = strings.TrimSpace(item.Name)
	item.Category = strings.TrimSpace(item.Category)
	item.State = normalizeState(item.State)

	if item.ID == "" {
		return errors.New("id is required")
	}
	if item.Code == "" {
		return errors.New("code is required")
	}
	if item.Name == "" {
		return errors.New("name is required")
	}
	if item.Category == "" {
		item.Category = "Pantry"
	}

	s.items[item.ID] = item
	return nil
}

func (s *MemoryStore) DeleteItem(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	id = strings.TrimSpace(id)
	if id == "" {
		return errors.New("id is required")
	}

	if _, ok := s.items[id]; !ok {
		return errors.New("item not found")
	}

	delete(s.items, id)
	return nil
}

func (s *MemoryStore) SetItemState(id, state string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	item, ok := s.items[id]
	if !ok {
		return errors.New("item not found")
	}

	item.State = normalizeState(state)
	s.items[id] = item
	return nil
}

func (s *MemoryStore) SetStateFromMQTT(id, state string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	item, ok := s.items[id]
	if !ok {
		return
	}

	item.State = normalizeState(state)
	s.items[id] = item
}

func (s *MemoryStore) ActiveItems() []GroceryItem {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var result []GroceryItem
	for _, item := range s.items {
		if !item.Archived {
			result = append(result, item)
		}
	}

	sort.Slice(result, func(i, j int) bool {
		return itemNumericID(result[i]) < itemNumericID(result[j])
	})

	return result
}

func (s *MemoryStore) MissingItems() []GroceryItem {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var result []GroceryItem
	for _, item := range s.items {
		if !item.Archived && item.State == "MISSING" {
			result = append(result, item)
		}
	}

	sort.Slice(result, func(i, j int) bool {
		return itemNumericID(result[i]) < itemNumericID(result[j])
	})

	return result
}

func (s *MemoryStore) Counts() (missing int, active int, archived int) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	for _, item := range s.items {
		if item.Archived {
			archived++
			continue
		}
		active++
		if item.State == "MISSING" {
			missing++
		}
	}

	return
}

func (s *MemoryStore) GetItem(id string) (GroceryItem, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	item, ok := s.items[id]
	return item, ok
}
func (s *MemoryStore) NextAvailableID() (string, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	for id := 1; id <= 99; id++ {
		key := strconv.Itoa(id)
		if _, exists := s.items[key]; !exists {
			return key, nil
		}
	}

	return "", errors.New("catalog is full")
}
func (s *MemoryStore) DeleteItemIfExists(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	id = strings.TrimSpace(id)
	if id == "" {
		return
	}

	delete(s.items, id)
}
func formatTwoDigitCode(id string) string {
	n, err := strconv.Atoi(strings.TrimSpace(id))
	if err != nil {
		return id
	}
	return fmt.Sprintf("%02d", n)
}