package networking

import (
	"bufio"
	"errors"
	"fmt"
	"github.com/maksimkurb/keen-pbr/src/internal/config"
	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"io"
	"net/netip"
	"os/exec"
	"sync"
)

const ipsetCommand = "ipset"

type IPSet struct {
	Name     string
	IpFamily config.IpFamily
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

func BuildIPSet(name string, ipFamily config.IpFamily) *IPSet {
	return &IPSet{
		Name:     name,
		IpFamily: ipFamily,
	}
}

func (ipset *IPSet) String() string {
	return fmt.Sprintf("ipset %s (IPv%d)", ipset.Name, ipset.IpFamily)
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
	if ipset.IpFamily == 6 {
		family = "inet6"
	} else {
		family = "inet"
	}

	cmd := exec.Command(ipsetCommand, "create", ipset.Name, "hash:net", "family", family, "-exist")
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

// AddToIpset adds the given networks to the specified ipset
func AddToIpset(ipset *config.IPSetConfig, networks []netip.Prefix) error {
	if _, err := exec.LookPath(ipsetCommand); err != nil {
		return fmt.Errorf("failed to find ipset command %s: %v", ipsetCommand, err)
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
			close(errCh) // Close the channel when the goroutine finishes
		}()

		// Write commands to stdin
		if ipset.FlushBeforeApplying {
			if _, err := fmt.Fprintf(stdin, "flush %s\n", ipset.IPSetName); err != nil {
				log.Warnf("failed to flush ipset %s: %v", ipset.IPSetName, err)
			}
		}

		errorCounter := 0
		for _, network := range networks {
			if !network.IsValid() {
				log.Warnf("skipping invalid network %v", network)
				continue
			}
			if _, err := fmt.Fprintf(stdin, "add %s %s\n", ipset.IPSetName, network.String()); err != nil {
				log.Warnf("failed to add address %s to ipset %s: %v", network, ipset.IPSetName, err)
				errorCounter++

				if errorCounter > 10 {
					errCh <- fmt.Errorf("too many errors, aborting import")
					return
				}
			}
		}
	}()

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add addresses to ipset %s: %v\n%s", ipset.IPSetName, err, output)
	}

	for err := range errCh {
		if err != nil {
			return err
		}
	}

	return nil
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

// AddWithTTL adds a single IP address or network to an ipset with a TTL (timeout).
// The TTL is specified in seconds. If TTL is 0, no timeout is set.
// Note: The ipset must be created with "timeout" option for TTL to work.
func AddWithTTL(ipsetName string, network netip.Prefix, ttlSeconds uint32) error {
	if _, err := exec.LookPath(ipsetCommand); err != nil {
		return fmt.Errorf("failed to find ipset command: %v", err)
	}

	if !network.IsValid() {
		return fmt.Errorf("invalid network: %v", network)
	}

	var cmd *exec.Cmd
	if ttlSeconds > 0 {
		cmd = exec.Command(ipsetCommand, "add", ipsetName, network.String(),
			"timeout", fmt.Sprintf("%d", ttlSeconds), "-exist")
	} else {
		cmd = exec.Command(ipsetCommand, "add", ipsetName, network.String(), "-exist")
	}

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add %s to ipset %s: %v\n%s",
			network, ipsetName, err, output)
	}

	return nil
}

// CreateWithTimeout creates an ipset with timeout support.
// This is required for TTL-based entries from DNS proxy.
func CreateWithTimeout(name string, ipFamily config.IpFamily, defaultTimeout uint32) error {
	if _, err := exec.LookPath(ipsetCommand); err != nil {
		return fmt.Errorf("failed to find ipset command: %v", err)
	}

	var family string
	if ipFamily == 6 {
		family = "inet6"
	} else {
		family = "inet"
	}

	cmd := exec.Command(ipsetCommand, "create", name, "hash:net",
		"family", family,
		"timeout", fmt.Sprintf("%d", defaultTimeout),
		"-exist")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to create ipset %s: %v\n%s", name, err, output)
	}

	return nil
}
