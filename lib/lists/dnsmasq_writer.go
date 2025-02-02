package lists

import (
	"bufio"
	"fmt"
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"github.com/maksimkurb/keenetic-pbr/lib/utils"
	"os"
	"path/filepath"
)

type DnsmasqConfigID rune

type DnsmasqConfigWriter struct {
	config      *config.Config
	ipsetCount  int
	dnsmasqDir  string
	fileMap     map[DnsmasqConfigID]*os.File
	fileBuffers map[DnsmasqConfigID]*bufio.Writer
}

func NewDnsmasqConfigWriter(config *config.Config) *DnsmasqConfigWriter {
	return &DnsmasqConfigWriter{
		config:      config,
		ipsetCount:  len(config.IPSets),
		dnsmasqDir:  config.GetAbsDnsmasqDir(),
		fileMap:     make(map[DnsmasqConfigID]*os.File),
		fileBuffers: make(map[DnsmasqConfigID]*bufio.Writer),
	}
}

func getConfigID(domain string) DnsmasqConfigID {
	if len(domain) == 0 {
		return '0'
	}
	return DnsmasqConfigID(domain[0])
}

func (w *DnsmasqConfigWriter) Close() {
	for _, b := range w.fileBuffers {
		err := b.Flush()
		if err != nil {
			log.Errorf("Failed to flush buffer: %v", err)
		}
	}
	for _, f := range w.fileMap {
		if err := f.Close(); err != nil {
			log.Errorf("Failed to close file: %v", err)
		}
	}
}

func (w *DnsmasqConfigWriter) GetIPSetCount() int {
	return w.ipsetCount
}

func (w *DnsmasqConfigWriter) getWriter(configID DnsmasqConfigID) *bufio.Writer {
	if w.fileBuffers[configID] == nil {
		path := filepath.Join(w.dnsmasqDir, fmt.Sprintf("%c.keenetic-pbr.conf", configID))
		f, err := os.Create(path)
		if err != nil {
			log.Fatalf("Failed to create dnsmasq cfg file '%s': %v", path, err)
			return nil
		}
		w.fileMap[configID] = f
		w.fileBuffers[configID] = bufio.NewWriter(f)
	}
	return w.fileBuffers[configID]
}

func (w *DnsmasqConfigWriter) WriteDomain(configID DnsmasqConfigID, domainStr SanitizedDomain, associations utils.BitSet) error {
	writer := w.getWriter(configID)

	if _, err := fmt.Fprintf(writer, "ipset=/%s/", domainStr); err != nil {
		return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
	}

	isFirstIPSet := true
	for i := 0; i < w.ipsetCount; i++ {
		if !associations.Has(i) {
			continue
		}

		if !isFirstIPSet {
			if _, err := writer.WriteRune(','); err != nil {
				return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
			}
		}

		isFirstIPSet = false

		if _, err := writer.WriteString(w.config.IPSets[i].IPSetName); err != nil {
			return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
		}
	}

	if _, err := writer.WriteRune('\n'); err != nil {
		return fmt.Errorf("failed to write to dnsmasq cfg file: %v", err)
	}

	return nil
}
