package lists

import (
	"testing"
)

func TestCreateDomainStore(t *testing.T) {
	ipsetCount := 3
	store := CreateDomainStore(ipsetCount)

	if store == nil {
		t.Error("Expected store to be non-nil")
		return
	}

	if store.ipsetCount != ipsetCount {
		t.Errorf("Expected ipsetCount to be %d, got %d", ipsetCount, store.ipsetCount)
	}

	if store.domainsCount != 0 {
		t.Errorf("Expected initial domainsCount to be 0, got %d", store.domainsCount)
	}

	if store.mapping == nil {
		t.Error("Expected mapping to be initialized")
	}

	if store.collisionTable != nil {
		t.Error("Expected collisionTable to be nil initially")
	}
}

func TestDomainStore_AssociateDomainWithIPSets(t *testing.T) {
	store := CreateDomainStore(2)

	ipsets := []DestIPSet{
		{Index: 0, Name: "ipset0"},
		{Index: 1, Name: "ipset1"},
	}

	domain := sanitizeDomain("example.com")

	// Associate domain with ipsets
	store.AssociateDomainWithIPSets(domain, ipsets)

	if store.domainsCount != 1 {
		t.Errorf("Expected domainsCount to be 1, got %d", store.domainsCount)
	}

	// Check that domain is associated with both ipsets
	bitSet, hash := store.GetAssociatedIPSetIndexesForDomain(domain)
	if bitSet == nil {
		t.Error("Expected bitSet to be non-nil")
	}

	if hash == 0 {
		t.Error("Expected hash to be non-zero")
	}

	// Verify both ipset indexes are set
	if !bitSet.Has(0) {
		t.Error("Expected ipset index 0 to be set")
	}

	if !bitSet.Has(1) {
		t.Error("Expected ipset index 1 to be set")
	}
}

func TestDomainStore_AssociateDomainWithIPSets_SameDomain(t *testing.T) {
	store := CreateDomainStore(1)

	ipsets := []DestIPSet{
		{Index: 0, Name: "ipset0"},
	}

	domain := sanitizeDomain("example.com")

	// Associate same domain twice
	store.AssociateDomainWithIPSets(domain, ipsets)
	store.AssociateDomainWithIPSets(domain, ipsets)

	// Should still count as 1 domain
	if store.domainsCount != 1 {
		t.Errorf("Expected domainsCount to be 1, got %d", store.domainsCount)
	}

	// Should have collision table initialized
	if store.collisionTable == nil {
		t.Error("Expected collision table to be initialized")
	}
}

func TestDomainStore_GetAssociatedIPSetIndexesForDomain_NotFound(t *testing.T) {
	store := CreateDomainStore(1)

	domain := sanitizeDomain("nonexistent.com")

	bitSet, hash := store.GetAssociatedIPSetIndexesForDomain(domain)

	if bitSet != nil {
		t.Error("Expected bitSet to be nil for non-existent domain")
	}

	if hash != 0 {
		t.Error("Expected hash to be 0 for non-existent domain")
	}
}

func TestDomainStore_Forget(t *testing.T) {
	store := CreateDomainStore(1)

	ipsets := []DestIPSet{
		{Index: 0, Name: "ipset0"},
	}

	domain := sanitizeDomain("example.com")

	// Associate domain
	store.AssociateDomainWithIPSets(domain, ipsets)

	// Get hash
	_, hash := store.GetAssociatedIPSetIndexesForDomain(domain)

	// Forget domain
	store.Forget(hash)

	// Verify domain is forgotten
	bitSet, _ := store.GetAssociatedIPSetIndexesForDomain(domain)
	if bitSet != nil {
		t.Error("Expected domain to be forgotten")
	}
}

func TestDomainStore_Count(t *testing.T) {
	store := CreateDomainStore(1)

	if store.Count() != 0 {
		t.Errorf("Expected initial count to be 0, got %d", store.Count())
	}

	ipsets := []DestIPSet{
		{Index: 0, Name: "ipset0"},
	}

	// Add domains
	store.AssociateDomainWithIPSets(sanitizeDomain("example.com"), ipsets)
	store.AssociateDomainWithIPSets(sanitizeDomain("test.org"), ipsets)

	if store.Count() != 2 {
		t.Errorf("Expected count to be 2, got %d", store.Count())
	}
}

func TestDomainStore_GetCollisionDomain_NoCollisions(t *testing.T) {
	store := CreateDomainStore(1)

	domain := sanitizeDomain("example.com")

	collision := store.GetCollisionDomain(domain)
	if collision != "" {
		t.Errorf("Expected no collision, got '%s'", collision)
	}
}

func TestDomainStore_GetCollisionDomain_WithCollisions(t *testing.T) {
	store := CreateDomainStore(1)

	ipsets := []DestIPSet{
		{Index: 0, Name: "ipset0"},
	}

	domain1 := sanitizeDomain("example.com")
	domain2 := sanitizeDomain("test.org")

	// Associate first domain
	store.AssociateDomainWithIPSets(domain1, ipsets)

	// Force a collision by manually setting collision table
	store.collisionTable = make(map[uint32]SanitizedDomain)
	hash := hashDomain(domain2)
	store.collisionTable[hash] = domain1

	// Check collision
	collision := store.GetCollisionDomain(domain2)
	if collision != domain1 {
		t.Errorf("Expected collision with '%s', got '%s'", domain1, collision)
	}

	// Same domain should not be a collision
	collision = store.GetCollisionDomain(domain1)
	if collision != "" {
		t.Errorf("Expected no collision for same domain, got '%s'", collision)
	}
}

func TestSanitizeDomain(t *testing.T) {
	tests := []struct {
		name     string
		input    string
		expected SanitizedDomain
	}{
		{"Lowercase", "example.com", "example.com"},
		{"Uppercase", "EXAMPLE.COM", "example.com"},
		{"Mixed case", "ExAmPlE.CoM", "example.com"},
		{"With subdomain", "Sub.Example.COM", "sub.example.com"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := sanitizeDomain(tt.input)
			if result != tt.expected {
				t.Errorf("Expected '%s', got '%s'", tt.expected, result)
			}
		})
	}
}

func TestHashDomain(t *testing.T) {
	domain1 := SanitizedDomain("example.com")
	domain2 := SanitizedDomain("test.org")

	hash1 := hashDomain(domain1)
	hash2 := hashDomain(domain2)

	// Hashes should be non-zero
	if hash1 == 0 {
		t.Error("Expected hash1 to be non-zero")
	}

	if hash2 == 0 {
		t.Error("Expected hash2 to be non-zero")
	}

	// Different domains should have different hashes (usually)
	if hash1 == hash2 {
		t.Log("Hash collision detected (rare but possible)")
	}

	// Same domain should always have same hash
	hash1_repeat := hashDomain(domain1)
	if hash1 != hash1_repeat {
		t.Error("Expected same domain to produce same hash")
	}
}

func TestDomainStore_AppendCollision(t *testing.T) {
	store := CreateDomainStore(1)

	domain := sanitizeDomain("example.com")
	hash := hashDomain(domain)

	// Test collision table creation
	store.appendCollision(domain, hash)

	if store.collisionTable == nil {
		t.Error("Expected collision table to be created")
	}

	if store.collisionTable[hash] != domain {
		t.Errorf("Expected collision table to contain domain '%s'", domain)
	}

	// Test adding another collision
	domain2 := sanitizeDomain("test.org")
	hash2 := hashDomain(domain2)

	store.appendCollision(domain2, hash2)

	if store.collisionTable[hash2] != domain2 {
		t.Errorf("Expected collision table to contain domain '%s'", domain2)
	}
}
