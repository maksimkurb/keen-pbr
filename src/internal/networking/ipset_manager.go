package networking

import (
	"net/netip"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
)

// IPSetManagerImpl implements the domain.IPSetManager interface.
//
// It provides a facade over the low-level IPSet operations, implementing
// the interface required by the service layer.
type IPSetManagerImpl struct{}

// NewIPSetManager creates a new IPSet manager.
func NewIPSetManager() *IPSetManagerImpl {
	return &IPSetManagerImpl{}
}

// Create creates a new ipset with the specified name and IP family.
// If the ipset already exists, this is a no-op.
func (m *IPSetManagerImpl) Create(name string, family config.IPFamily) error {
	ipset := BuildIPSet(name, family)
	return ipset.CreateIfNotExists()
}

// Flush removes all entries from the specified ipset.
func (m *IPSetManagerImpl) Flush(name string) error {
	// We need to determine IP family, but Flush doesn't depend on it for deletion
	// Try both IPv4 and IPv6, ignore errors if ipset doesn't exist
	ipsetV4 := BuildIPSet(name, config.Ipv4)
	if err := ipsetV4.Flush(); err != nil {
		// Try IPv6 in case it's an IPv6 ipset
		ipsetV6 := BuildIPSet(name, config.Ipv6)
		return ipsetV6.Flush()
	}
	return nil
}

// Import adds a list of IP networks to the specified ipset configuration.
func (m *IPSetManagerImpl) Import(ipsetCfg *config.IPSetConfig, networks []netip.Prefix) error {
	ipset := BuildIPSet(ipsetCfg.IPSetName, ipsetCfg.IPVersion)

	// Open writer for bulk import
	writer, err := ipset.OpenWriter()
	if err != nil {
		return err
	}
	defer utils.CloseOrWarn(writer)

	// Add each network
	for _, network := range networks {
		// Filter by IP family
		if (network.Addr().Is4() && ipsetCfg.IPVersion == config.Ipv4) ||
			(network.Addr().Is6() && ipsetCfg.IPVersion == config.Ipv6) {
			if err := writer.Add(network); err != nil {
				return err
			}
		}
	}

	return nil
}

// CreateIfAbsent ensures all ipsets defined in the configuration exist.
func (m *IPSetManagerImpl) CreateIfAbsent(cfg *config.Config) error {
	for _, ipsetCfg := range cfg.IPSets {
		log.Debugf("Creating ipset %s if absent", ipsetCfg.IPSetName)
		if err := m.Create(ipsetCfg.IPSetName, ipsetCfg.IPVersion); err != nil {
			return err
		}
	}
	return nil
}
