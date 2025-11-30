package utils

import "strings"

// MatchDomain checks if sourceDomain matches matchesDomain and returns
// whether there's a match and the specificity level.
//
// Specificity indicates how many domain parts (labels) matched:
// - "some.sub.domain.com" matches "com" with specificity 1
// - "some.sub.domain.com" matches "domain.com" with specificity 2
// - "some.sub.domain.com" matches "sub.domain.com" with specificity 3
// - "some.sub.domain.com" matches "some.sub.domain.com" with specificity 4
//
// The function performs suffix matching, so "domain.com" will match
// "sub.domain.com" but not "otherdomain.com".
func MatchDomain(sourceDomain, matchesDomain string) (matches bool, specificity uint8) {
	sourceDomain = strings.ToLower(sourceDomain)
	matchesDomain = strings.ToLower(matchesDomain)

	// Check exact match first
	if sourceDomain == matchesDomain {
		parts := strings.Split(sourceDomain, ".")
		return true, uint8(len(parts))
	}

	// Check if matchesDomain is a suffix of sourceDomain
	// For "some.sub.domain.com" to match "domain.com":
	// - sourceDomain must end with ".domain.com"
	// - OR sourceDomain must equal "domain.com" (already checked above)
	if !strings.HasSuffix(sourceDomain, "."+matchesDomain) {
		return false, 0
	}

	// Calculate specificity as the number of parts in matchesDomain
	parts := strings.Split(matchesDomain, ".")
	return true, uint8(len(parts))
}
