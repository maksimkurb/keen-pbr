package lib

import (
	"fmt"
	"os/exec"
)

type IpsetManager struct{}

func (im *IpsetManager) AddToIpset(ipsetCommand, name string, networks []string) error {
	cmd := exec.Command(ipsetCommand, "restore", "-exist")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("Failed to get stdin pipe: %v", err)
	}

	go func() {
		defer stdin.Close()
		fmt.Fprintf(stdin, "create %s hash:net family inet\n", name)
		for _, network := range networks {
			fmt.Fprintf(stdin, "add %s %s\n", name, network)
		}
	}()

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("Failed to add addresses to ipset %s: %v\n%s", name, err, output)
	}

	return nil
}
