/**
 * Whether `locationPath` corresponds to nav item `href`,
 * including child routes (`/lists/create` belongs to Lists `/lists`).
 * Root `/` matches only exactly.
 */
export function matchesNavHref(locationPath: string, href: string): boolean {
  if (href === "/") {
    return locationPath === "/" || locationPath === ""
  }
  return locationPath === href || locationPath.startsWith(`${href}/`)
}
