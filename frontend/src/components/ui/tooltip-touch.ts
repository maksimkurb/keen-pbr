export class TooltipTouchGesture {
  private timer: ReturnType<typeof setTimeout> | undefined
  private pointer: {
    id: number
    x: number
    y: number
    hasAction: boolean
  } | null = null
  private clickSuppressed = false
  private show: ((toggle: boolean) => void) | null = null

  get suppressClick() {
    return this.clickSuppressed
  }

  reset() {
    this.cancel()
    this.clickSuppressed = false
  }

  consumeClick(detail: number) {
    if (!this.clickSuppressed || detail === 0) return false
    this.clickSuppressed = false
    return true
  }

  start(
    id: number,
    x: number,
    y: number,
    hasAction: boolean,
    show: (toggle: boolean) => void
  ) {
    this.reset()
    this.show = show
    this.pointer = { id, x, y, hasAction }
    if (hasAction) {
      this.timer = setTimeout(() => this.hold(), 500)
    }
  }

  move(id: number, x: number, y: number) {
    const pointer = this.pointer
    if (pointer?.id === id && Math.hypot(x - pointer.x, y - pointer.y) > 10) {
      this.cancel()
    }
  }

  end(id: number) {
    if (this.pointer?.id !== id) return
    if (!this.pointer.hasAction) {
      this.clickSuppressed = true
      this.show?.(true)
    }
    this.cancel()
  }

  hold() {
    if (!this.pointer?.hasAction || this.suppressClick) return
    clearTimeout(this.timer)
    this.clickSuppressed = true
    this.show?.(false)
  }

  contextMenu() {
    if (!this.pointer && !this.suppressClick) return false
    this.hold()
    return true
  }

  cancel() {
    clearTimeout(this.timer)
    this.timer = undefined
    this.pointer = null
    this.show = null
  }
}
