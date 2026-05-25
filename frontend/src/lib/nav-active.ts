export function matchesNavHref(locationPath: string, href: string): boolean {
  if (href === "/") {
    return locationPath === "/" || locationPath === ""
  }

  return locationPath === href || locationPath.startsWith(`${href}/`)
}
