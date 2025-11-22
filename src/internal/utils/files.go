package utils

import (
	"io"
)

func CloseOrPanic(file io.Closer) {
	if err := file.Close(); err != nil {
		panic(err)
	}
}
