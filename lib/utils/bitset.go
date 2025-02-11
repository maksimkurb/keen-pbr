package utils

type bitSet struct {
	len   int
	array []uint8
}

type BitSet interface {
	Has(pos int) bool
	Add(pos int) bool
	Remove(pos int) bool
	Len() int
	Count() int
	Clear()
	Toggle(pos int) bool
}

func NewBitSet(length int) BitSet {
	if length < 0 {
		panic("BitSet length must be non-negative")
	}
	return &bitSet{
		len:   length,
		array: make([]uint8, (length+7)/8),
	}
}

// Has checks whether the bit at the given position is set.
func (b *bitSet) Has(pos int) bool {
	if pos < 0 || pos >= b.len {
		return false
	}
	word, bit := pos/64, pos%64
	return (b.array[word] & (1 << bit)) != 0
}

// Add sets the bit at the given position. Returns true if the bit was already set.
func (b *bitSet) Add(pos int) bool {
	if pos < 0 || pos >= b.len {
		return false
	}
	word, bit := pos/64, pos%64
	alreadySet := (b.array[word] & (1 << bit)) != 0
	b.array[word] |= (1 << bit)
	return alreadySet
}

// Remove clears the bit at the given position. Returns true if the bit was previously set.
func (b *bitSet) Remove(pos int) bool {
	if pos < 0 || pos >= b.len {
		return false
	}
	word, bit := pos/64, pos%64
	previouslySet := (b.array[word] & (1 << bit)) != 0
	b.array[word] &^= (1 << bit)
	return previouslySet
}

// Len returns the length of the bit set.
func (b *bitSet) Len() int {
	return b.len
}

// Count returns the number of set bits.
func (b *bitSet) Count() int {
	count := 0
	for _, word := range b.array {
		count += popCount(word)
	}
	return count
}

// Clear resets all bits in the bit set.
func (b *bitSet) Clear() {
	for i := range b.array {
		b.array[i] = 0
	}
}

// Toggle flips the bit at the given position. Returns true if the bit is now set.
func (b *bitSet) Toggle(pos int) bool {
	if pos < 0 || pos >= b.len {
		return false
	}
	word, bit := pos/64, pos%64
	b.array[word] ^= (1 << bit)
	return (b.array[word] & (1 << bit)) != 0
}

// popCount counts the number of set bits in a uint8.
func popCount(x uint8) int {
	count := 0
	for x != 0 {
		x &= x - 1
		count++
	}
	return count
}
