package api

import (
	"crypto/rand"
	"encoding/hex"
)

// generateID generates a random ID
func generateID() string {
	b := make([]byte, 8)
	rand.Read(b)
	return hex.EncodeToString(b)
}
