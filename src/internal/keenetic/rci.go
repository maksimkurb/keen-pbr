package keenetic

import (
	"bytes"
	"net/http"
)

const (
	dnsServerPrefix  = "dns_server = "
	localhostPrefix  = "127.0.0.1:"
	httpsPrefix      = "https://"
	atSymbol         = "@"
	dotSymbol        = "."
	commentDelimiter = "#"
)

// HTTPClient interface for dependency injection in tests
type HTTPClient interface {
	Get(url string) (*http.Response, error)
	Post(url string, contentType string, body []byte) (*http.Response, error)
}

// defaultHTTPClient implements HTTPClient using the standard http package
type defaultHTTPClient struct{}

func (c *defaultHTTPClient) Get(url string) (*http.Response, error) {
	return http.Get(url)
}

func (c *defaultHTTPClient) Post(url string, contentType string, body []byte) (*http.Response, error) {
	return http.Post(url, contentType, bytes.NewReader(body))
}

// KeeneticVersion represents the version of Keenetic OS
type KeeneticVersion struct {
	Major int
	Minor int
}

