package networking

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"net/netip"
	"os/exec"
	"sync"

	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

const ipsetCommand = "ipset"

type IPSet struct {
	Name     string
	IPFamily config.IPFamily
}

type IPSetWriter struct {
	ipset  *IPSet
	cmd    *exec.Cmd
	stdin  *bufio.Writer
	pipe   *io.WriteCloser
	mutex  sync.Mutex
	errors chan error
	closed bool
}

func BuildIPSet(name string, ipFamily config.IPFamily) *IPSet {
	return &IPSet{
		Name:     name,
		IPFamily: ipFamily,
	}
}

func (ipset *IPSet) String() string {
	return fmt.Sprintf("ipset %s (IPv%d)", ipset.Name, ipset.IPFamily)
}

func (ipset *IPSet) CheckExecutable() error {
	if _, err := exec.LookPath("ipset"); err != nil {
		return fmt.Errorf("failed to find ipset command: %v", err)
	}
	return nil
}

func (ipset *IPSet) CreateIfNotExists() error {
	if err := ipset.CheckExecutable(); err != nil {
		return err
	}

	var family string
	if ipset.IPFamily == 6 {
		family = "inet6"
	} else {
		family = "inet"
	}

	// Always create with timeout support (default timeout=0 means permanent entries)
	// This allows both permanent entries from lists and TTL-based entries from DNS proxy
	cmd := exec.Command(ipsetCommand, "create", ipset.Name, "hash:net",
		"family", family, "timeout", "0", "-exist")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to create ipset [%s]: %v", ipset, err)
	}

	return nil
}

func (ipset *IPSet) IsExists() (bool, error) {
	if err := ipset.CheckExecutable(); err != nil {
		return false, err
	}

	cmd := exec.Command(ipsetCommand, "-n", "list", ipset.Name)
	if err := cmd.Start(); err != nil {
		return false, err
	}

	if err := cmd.Wait(); err != nil {
		if exiterr, ok := err.(*exec.ExitError); ok {
			return exiterr.ExitCode() == 0, nil
		} else {
			return false, err
		}
	} else {
		return true, nil
	}
}

func (ipset *IPSet) Flush() error {
	if err := ipset.CheckExecutable(); err != nil {
		return err
	}

	if exists, err := ipset.IsExists(); err != nil {
		return err
	} else if !exists {
		return fmt.Errorf("ipset %s does not exist, failed to flush", ipset.Name)
	}

	cmd := exec.Command(ipsetCommand, "flush", ipset.Name)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to flush ipset %s: %v", ipset.Name, err)
	}
	return nil
}

func (ipset *IPSet) OpenWriter() (*IPSetWriter, error) {
	if err := ipset.CheckExecutable(); err != nil {
		return nil, err
	}

	cmd := exec.Command("ipset", "restore", "-exist")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("failed to get stdin pipe: %v", err)
	}

	writer := &IPSetWriter{
		ipset:  ipset,
		cmd:    cmd,
		stdin:  bufio.NewWriter(stdin),
		pipe:   &stdin,
		errors: make(chan error),
	}

	go func() {
		defer close(writer.errors)
		if err := cmd.Run(); err != nil {
			writer.errors <- fmt.Errorf("ipset command failed: %v", err)
		}
	}()

	return writer, nil
}

// Add writes an IP network to the buffer.
func (w *IPSetWriter) Add(network netip.Prefix) error {
	w.mutex.Lock()
	defer w.mutex.Unlock()

	if w.closed {
		return errors.New("cannot add to closed writer")
	}

	if !network.IsValid() {
		log.Warnf("Skipping invalid network: %v", network)
		return nil
	}

	if _, err := w.stdin.WriteString(fmt.Sprintf("add %s %s\n", w.ipset.Name, network.String())); err != nil {
		return fmt.Errorf("failed to write to ipset: %v", err)
	}
	return nil
}

// Close flushes the buffer, closes the pipe, and waits for the command to complete.
func (w *IPSetWriter) Close() error {
	w.mutex.Lock()
	defer w.mutex.Unlock()

	if w.closed {
		return errors.New("writer already closed")
	}
	w.closed = true

	if err := w.stdin.Flush(); err != nil {
		return fmt.Errorf("failed to flush stdin: %v", err)
	}

	if err := (*w.pipe).Close(); err != nil {
		return fmt.Errorf("failed to close stdin pipe: %v", err)
	}

	for err := range w.errors {
		if err != nil {
			return err
		}
	}

	return nil
}

func (w *IPSetWriter) GetIPSet() *IPSet {
	return w.ipset
}

// IPSetEntry represents a single entry to be added to an ipset.
type IPSetEntry struct {
	IPSetName string
	Network   netip.Prefix
	TTL       uint32
}

// BatchAddWithTTL adds multiple entries to ipsets in a single command.
func BatchAddWithTTL(entries []IPSetEntry) error {
	if len(entries) == 0 {
		return nil
	}

	if _, err := exec.LookPath(ipsetCommand); err != nil {
		return fmt.Errorf("failed to find ipset command: %v", err)
	}

	cmd := exec.Command(ipsetCommand, "restore", "-exist")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("failed to get stdin pipe: %v", err)
	}

	errCh := make(chan error, 1)
	go func() {
		defer func() {
			if err := stdin.Close(); err != nil {
				errCh <- fmt.Errorf("failed to close stdin pipe: %v", err)
			}
			close(errCh)
		}()

		for _, entry := range entries {
			if !entry.Network.IsValid() {
				log.Warnf("skipping invalid network %v", entry.Network)
				continue
			}

			var line string
			if entry.TTL > 0 {
				line = fmt.Sprintf("add %s %s timeout %d\n", entry.IPSetName, entry.Network.String(), entry.TTL)
			} else {
				line = fmt.Sprintf("add %s %s\n", entry.IPSetName, entry.Network.String())
			}

			if _, err := io.WriteString(stdin, line); err != nil {
				errCh <- fmt.Errorf("failed to write to stdin: %v", err)
				return
			}
		}
	}()

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to batch add to ipsets: %v\n%s", err, output)
	}

	for err := range errCh {
		if err != nil {
			return err
		}
	}

	return nil
}
