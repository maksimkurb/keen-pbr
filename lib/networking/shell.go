package networking

import (
	"bytes"
	"fmt"
	"github.com/maksimkurb/keen-pbr/lib/log"
	"os"
	"os/exec"
)

func RunShellScript(script string, envVars map[string]string) (string, error) {
	log.Infof("Running shell script '%s' with the following environment variables: %v", script, envVars)

	// Create the command to run the script
	cmd := exec.Command("sh", "-c", script)

	// Copy the current environment
	env := os.Environ()

	// Add or override the specified environment variables
	for key, value := range envVars {
		env = append(env, fmt.Sprintf("%s=%s", key, value))
	}
	cmd.Env = env

	// Capture the output
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	// Run the command
	err := cmd.Run()
	if err != nil {
		return stderr.String(), fmt.Errorf("failed to execute script: %v", err)
	}

	return stdout.String(), nil
}
