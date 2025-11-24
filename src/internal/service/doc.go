// Package service provides business logic orchestration layer for keen-pbr.
//
// This package contains service layer components that orchestrate operations across
// multiple domain managers and enforce business rules. Services act as a bridge between
// the command layer (CLI/API) and the domain layer (networking, keenetic, etc.).
//
// # Key Services
//
// DNSService: Unified DNS server retrieval and formatting for both CLI and API.
//
// InterfaceService: Network interface information retrieval and formatting.
//
// IPSetService: IPSet operations orchestration (creation, population, validation).
//
// # Example Usage
//
// Creating and using a service:
//
//	deps := domain.NewDefaultDependencies()
//	dnsService := service.NewDNSService(deps.KeeneticClient())
//
//	servers, err := dnsService.GetDNSServers()
//	if err != nil {
//	    log.Fatal(err)
//	}
//
//	formatted := dnsService.FormatDNSServers(servers)
//	fmt.Println(formatted)
package service
