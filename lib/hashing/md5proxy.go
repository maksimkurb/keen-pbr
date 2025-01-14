package hashing

import (
	"crypto/md5"
	"encoding/hex"
	"hash"
	"io"
)

type ChecksumProvider interface {
	GetChecksum() (string, error)
}

// ChecksumReaderProxy is a proxy that calculates the MD5 checksum of data as it's read.
type ChecksumReaderProxy struct {
	reader      io.Reader
	checksum    hash.Hash
	checksumErr error
}

// NewMD5ReaderProxy creates a new instance of ChecksumReaderProxy.
func NewMD5ReaderProxy(reader io.Reader) *ChecksumReaderProxy {
	return &ChecksumReaderProxy{
		reader:   reader,
		checksum: md5.New(),
	}
}

// Read reads data from the underlying reader, calculates the MD5 checksum
func (p *ChecksumReaderProxy) Read(buf []byte) (int, error) {
	// Read data from the wrapped reader
	n, err := p.reader.Read(buf)
	if n > 0 {
		// Update checksum with the read bytes
		if _, checksumErr := p.checksum.Write(buf[:n]); checksumErr != nil {
			return n, checksumErr
		}
	}
	return n, err
}

// GetChecksum returns the calculated MD5 checksum as a hex string.
func (p *ChecksumReaderProxy) GetChecksum() (string, error) {
	if p.checksumErr == nil {
		return hex.EncodeToString(p.checksum.Sum(nil)), nil
	}
	return "", p.checksumErr
}

type ChecksumStringSetProxy struct {
	set         map[string]struct{}
	checksum    hash.Hash
	checksumErr error
}

func NewChecksumStringSet() *ChecksumStringSetProxy {
	return &ChecksumStringSetProxy{
		set:      make(map[string]struct{}, 0),
		checksum: md5.New(),
	}
}

func (p *ChecksumStringSetProxy) Put(str string) error {
	if _, err := p.checksum.Write([]byte(str + "\n")); err != nil {
		return err
	}
	p.set[str] = struct{}{}
	return nil
}

func (p *ChecksumStringSetProxy) Size() int {
	return len(p.set)
}

func (p *ChecksumStringSetProxy) Map() map[string]struct{} {
	return p.set
}

func (p *ChecksumStringSetProxy) GetChecksum() (string, error) {
	if p.checksumErr == nil {
		return hex.EncodeToString(p.checksum.Sum(nil)), nil
	}
	return "", p.checksumErr
}
