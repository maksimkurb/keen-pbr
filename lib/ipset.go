package lib

import (
	"fmt"
	"os/exec"
)

type IpsetManager struct{}

func (im *IpsetManager) AddToIpset(ipsetCommand string, ipset IpsetConfig, networks []string) error {
	cmd := exec.Command(ipsetCommand, "restore", "-exist")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("Failed to get stdin pipe: %v", err)
	}

	go func() {
		defer stdin.Close()
		fmt.Fprintf(stdin, "create %s hash:net family inet\n", ipset.IpsetName)

		if ipset.FlushBeforeApplying {
			fmt.Fprintf(stdin, "flush %s\n", ipset.IpsetName)
		}

		for _, network := range networks {
			fmt.Fprintf(stdin, "add %s %s\n", ipset.IpsetName, network)
		}
	}()

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("Failed to add addresses to ipset %s: %v\n%s", ipset.IpsetName, err, output)
	}

	return nil
}
