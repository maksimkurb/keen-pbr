package caching

// getAddressesForTest is a test helper that retrieves all addresses (both IPv4 and IPv6)
// for a domain using the optimized GetFilteredAddresses method.
// This tests the actual production code path.
func getAddressesForTest(cache *RecordsCache, domain string) []CachedAddress {
	var result []CachedAddress
	// Get IPv4 addresses (qtype = 1 is dns.TypeA)
	result = cache.GetFilteredAddresses(domain, 1, result)
	// Get IPv6 addresses (qtype = 28 is dns.TypeAAAA)
	result = cache.GetFilteredAddresses(domain, 28, result)
	return result
}
