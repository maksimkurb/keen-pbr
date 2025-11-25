package utils

import (
	"io"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
)

func CloseOrPanic(file io.Closer) {
	if err := file.Close(); err != nil {
		panic(err)
	}
}


func CloseOrWarn(file io.Closer) {
	if err := file.Close(); err != nil {
		log.Warnf("Failed to close file: %v", err)
	}
}
