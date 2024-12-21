package lib

import (
	"fmt"
	"log"
	"os/exec"
)

// CreateIpset creates a new ipset with the given name and IP family (4 or 6)
func CreateIpset(ipsetCommand string, ipset IpsetConfig) error {
	// Determine IP family
	family := "inet"
	if ipset.IpVersion == 6 {
		family = "inet6"
	} else if ipset.IpVersion != 0 && ipset.IpVersion != 4 {
		log.Printf("unknown IP version %d, assuming IPv4", ipset.IpVersion)
	}

	cmd := exec.Command(ipsetCommand, "create", ipset.IpsetName, "hash:net", "family", family, "-exist")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to create ipset %s (IPv%d): %v", ipset.IpsetName, ipset.IpVersion, err)
	}

	return nil
}

// AddToIpset adds the given networks to the specified ipset
func AddToIpset(ipsetCommand string, ipset IpsetConfig, networks []string) error {
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
		}()

		// Write commands to stdin
		if ipset.FlushBeforeApplying {
			if _, err := fmt.Fprintf(stdin, "flush %s\n", ipset.IpsetName); err != nil {
				log.Printf("failed to flush ipset %s: %v", ipset.IpsetName, err)
			}
		}

		errorCounter := 0
		for _, network := range networks {
			if _, err := fmt.Fprintf(stdin, "add %s %s\n", ipset.IpsetName, network); err != nil {
				log.Printf("failed to add address %s to ipset %s: %v", network, ipset.IpsetName, err)
				errorCounter++

				if errorCounter > 10 {
					errCh <- fmt.Errorf("too many errors, aborting import")
					return
				}
			}
		}
	}()

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add addresses to ipset %s: %v\n%s", ipset.IpsetName, err, output)
	}

	for err := range errCh {
		if err != nil {
			panic(err)
		}
	}

	return nil
}
