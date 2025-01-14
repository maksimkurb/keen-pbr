package lists

import (
	"errors"
	"github.com/maksimkurb/keenetic-pbr/lib/hashing"
	"github.com/maksimkurb/keenetic-pbr/lib/log"
	"io"
	"os"
)

func IsFileChanged(checksumProxy hashing.ChecksumProvider, filePath string) (bool, error) {
	if _, err := os.Stat(filePath); errors.Is(err, os.ErrNotExist) {
		return true, nil
	}

	if md5, err := checksumProxy.GetChecksum(); err != nil {
		return false, err
	} else {
		checksumFilePath := filePath + ".md5"
		checksum, err := readChecksum(checksumFilePath)
		if err != nil {
			log.Debugf("Failed to read checksum file '%s', assuming it's changed: %v", checksumFilePath, err)
			return true, nil
		}
		return string(checksum) != md5, nil
	}
}

func readChecksum(checksumFilePath string) ([]byte, error) {
	if checksumFile, err := os.Open(checksumFilePath); err != nil {
		return nil, err
	} else {
		defer checksumFile.Close()

		return io.ReadAll(checksumFile)
	}
}

func WriteChecksum(checksumProxy hashing.ChecksumProvider, filePath string) error {
	checksumFilePath := filePath + ".md5"
	if checksum, err := checksumProxy.GetChecksum(); err != nil {
		return err
	} else {
		return os.WriteFile(checksumFilePath, []byte(checksum), 0644)
	}
}
