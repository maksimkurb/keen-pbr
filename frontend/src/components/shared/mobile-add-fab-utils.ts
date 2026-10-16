export function getMobileFabCollapsedState(
  currentY: number,
  lastChangeY: number
): boolean | null {
  if (Math.abs(currentY - lastChangeY) < 12) return null
  return currentY >= 24 && currentY > lastChangeY
}
