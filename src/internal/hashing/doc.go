// Package hashing provides MD5 checksum calculation utilities.
//
// This package implements transparent proxies for calculating MD5 checksums
// of data streams and string collections. It's primarily used for detecting
// changes in downloaded IP lists to avoid unnecessary file writes and processing.
//
// # Components
//
//   - ChecksumReaderProxy: Calculates MD5 while reading from an io.Reader
//   - ChecksumStringSetProxy: Calculates MD5 of a set of strings
//   - ChecksumProvider: Interface for types that provide checksums
//
// # Example Usage
//
// Calculating checksum while reading HTTP response:
//
//	resp, _ := http.Get(url)
//	defer resp.Body.Close()
//
//	proxy := hashing.NewMD5ReaderProxy(resp.Body)
//	content, _ := io.ReadAll(proxy)
//
//	checksum, _ := proxy.GetChecksum()
//	fmt.Printf("Downloaded %d bytes, MD5: %s\n", len(content), checksum)
//
// Building a checksum of unique strings:
//
//	set := hashing.NewChecksumStringSet()
//	set.Put("192.168.1.0/24")
//	set.Put("10.0.0.0/8")
//
//	checksum, _ := set.GetChecksum()
//	fmt.Printf("Set has %d entries, MD5: %s\n", set.Size(), checksum)
//
// The proxy pattern allows checksum calculation without changing existing
// code that works with io.Reader interfaces. The checksum is computed
// incrementally as data is read, making it memory-efficient for large streams.
package hashing
