package dnsproxy

import "net"

// Handler defines the interface for processing DNS requests.
// The network server component calls this for every incoming query.
// This interface decouples the network layer from the business logic.
type Handler interface {
	// HandleRequest processes a raw DNS query and returns a raw DNS response.
	// Parameters:
	//   - clientAddr: The address of the client making the request
	//   - reqBytes: The raw DNS query packet
	//   - network: The network protocol ("udp" or "tcp")
	// Returns:
	//   - []byte: The raw DNS response packet to send back to the client
	//   - error: Any error that occurred during processing
	HandleRequest(clientAddr net.Addr, reqBytes []byte, network string) ([]byte, error)

	// Shutdown allows the handler to clean up its resources (e.g., close upstreams).
	Shutdown()

	// ReloadLists reloads domain lists in the matcher.
	ReloadLists()
}
