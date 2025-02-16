package lists

import (
	"github.com/maksimkurb/keen-pbr/lib/log"
	"github.com/maksimkurb/keen-pbr/lib/utils"
	"hash/crc32"
	"strings"
)

type SanitizedDomain string

type DomainStore struct {
	mapping        map[uint32]utils.BitSet
	ipsetCount     int
	domainsCount   int
	collisionTable map[uint32]SanitizedDomain
}

func CreateDomainStore(ipsetCount int) *DomainStore {
	return &DomainStore{
		mapping:        make(map[uint32]utils.BitSet),
		ipsetCount:     ipsetCount,
		collisionTable: nil,
	}
}

func (n *DomainStore) AssociateDomainWithIPSets(domainStr SanitizedDomain, ipsets []DestIPSet) {
	crc32Hash := hashDomain(domainStr)

	if _, ok := n.mapping[crc32Hash]; !ok {
		n.mapping[crc32Hash] = utils.NewBitSet(n.ipsetCount)
		n.domainsCount++
	} else {
		// if domain with the same hash appeared twice, add it to collision table
		n.appendCollision(domainStr, crc32Hash)
	}

	for _, ipset := range ipsets {
		n.mapping[crc32Hash].Add(ipset.Index)
	}
}

func (n *DomainStore) GetAssociatedIPSetIndexesForDomain(domainStr SanitizedDomain) (utils.BitSet, uint32) {
	crc32Hash := hashDomain(domainStr)

	if _, ok := n.mapping[crc32Hash]; !ok {
		return nil, uint32(0)
	}

	return n.mapping[crc32Hash], crc32Hash
}

func (n *DomainStore) Forget(hash uint32) {
	delete(n.mapping, hash)
}

func (n *DomainStore) Count() int {
	return n.domainsCount
}

func (n *DomainStore) appendCollision(domainStr SanitizedDomain, domainHash uint32) {
	log.Debugf("Adding domain [%s] with hash [%x] to collision table", domainStr, domainHash)

	if n.collisionTable == nil {
		n.collisionTable = make(map[uint32]SanitizedDomain)
	}

	n.collisionTable[domainHash] = domainStr
}

func (n *DomainStore) GetCollisionDomain(domainStr SanitizedDomain) SanitizedDomain {
	if n.collisionTable == nil {
		return ""
	}
	crc32Hash := hashDomain(domainStr)

	// if hash is present in the collision table, but domainStr is different, it's a collision
	if n.collisionTable[crc32Hash] != domainStr {
		return n.collisionTable[crc32Hash]
	}

	return ""
}

func sanitizeDomain(domainStr string) SanitizedDomain {
	return SanitizedDomain(strings.ToLower(domainStr))
}

func hashDomain(domainStr SanitizedDomain) uint32 {
	return crc32.ChecksumIEEE([]byte(domainStr))
}
