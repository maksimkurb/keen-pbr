import type { ListConfig } from "@/api/generated/model/listConfig"

/**
 * Returns true if domain `a` is covered by domain `b`.
 * `a` is covered by `b` when `a === b` or `a` ends with `"." + b`.
 * e.g. "test.te.ru" is covered by "te.ru"
 */
export function isCoveredBy(a: string, b: string): boolean {
  if (a === b) return true
  return a.endsWith("." + b)
}

/**
 * Returns entries that appear more than once in the list (de-duplicated,
 * case-insensitive normalised to lower-case).
 */
export function findExactDuplicates(entries: string[]): string[] {
  const lower = entries.map((e) => e.toLowerCase())
  const seen = new Set<string>()
  const duplicates = new Set<string>()

  for (const entry of lower) {
    if (seen.has(entry)) {
      duplicates.add(entry)
    } else {
      seen.add(entry)
    }
  }

  return Array.from(duplicates)
}

/**
 * Returns subdomain entries that are redundant because a parent domain is
 * already present in the same list.
 * For each entry `a`, if there exists another entry `b` (b !== a) such that
 * `a` is covered by `b`, then `a` is redundant.
 */
export function findRedundantSubdomains(
  domains: string[]
): { redundant: string; coveredBy: string }[] {
  const lower = domains.map((d) => d.toLowerCase())
  const results: { redundant: string; coveredBy: string }[] = []

  for (let i = 0; i < lower.length; i++) {
    const a = lower[i]
    for (let j = 0; j < lower.length; j++) {
      if (i === j) continue
      const b = lower[j]
      // a is strictly covered by b (not equal — exact duplicates are handled separately)
      if (a !== b && isCoveredBy(a, b)) {
        results.push({ redundant: domains[i], coveredBy: domains[j] })
        break
      }
    }
  }

  return results
}

/**
 * Returns per-entry cross-list usage: for each entry in `currentEntries`,
 * if that entry (or a parent domain of it) appears in another list's domains,
 * report the other list's name.
 * IP/CIDR entries are checked for exact matches only.
 */
export function findCrossListUsage(
  currentListId: string,
  currentEntries: string[],
  allLists: Record<string, ListConfig>
): { entry: string; otherList: string }[] {
  const results: { entry: string; otherList: string }[] = []

  for (const entry of currentEntries) {
    const entryLower = entry.toLowerCase()

    for (const [listId, listConfig] of Object.entries(allLists)) {
      if (listId === currentListId) continue

      const otherDomains = (listConfig.domains ?? []).map((d) => d.toLowerCase())
      const otherIpCidrs = (listConfig.ip_cidrs ?? []).map((d) => d.toLowerCase())

      // Check domains: entry is covered by (or covers) a domain in another list
      const domainMatch = otherDomains.some(
        (other) => isCoveredBy(entryLower, other) || isCoveredBy(other, entryLower)
      )

      // Check ip_cidrs: exact match only
      const ipMatch = otherIpCidrs.includes(entryLower)

      if (domainMatch || ipMatch) {
        results.push({ entry, otherList: listId })
      }
    }
  }

  return results
}
