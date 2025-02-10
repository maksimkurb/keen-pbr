package lists

import (
	"github.com/maksimkurb/keenetic-pbr/lib/config"
	"testing"
)

func BenchmarkApplyLists(b *testing.B) {
	cfg := createConfig("../../tests/ManyLists.conf")

	// Prepare data (download) only once before running the benchmark.
	if err := DownloadLists(cfg); err != nil {
		b.Fatalf("DownloadLists() error = %v", err)
	}

	// Reset the timer to exclude the setup time from the benchmark.
	b.ResetTimer()

	// Run the benchmark for ImportListsToIPSets.
	b.Run("ImportListsToIPSets", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			if err := ImportListsToIPSets(cfg, false, true); err != nil {
				b.Errorf("ImportListsToIPSets() error = %v", err)
			}
		}
	})
}

func createConfig(path string) *config.Config {
	if cfg, err := config.LoadConfig(path); err != nil {
		panic(err)
	} else {
		return cfg
	}
}
