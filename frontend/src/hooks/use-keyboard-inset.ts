import * as React from "react"

// Height of the part of the layout viewport hidden by the on-screen keyboard.
// Mobile browsers shrink only the visual viewport when the keyboard opens, so
// fixed elements anchored to the bottom (and dvh units) end up under it.
export function useKeyboardInset() {
  const [inset, setInset] = React.useState(0)

  React.useEffect(() => {
    const vv = window.visualViewport
    if (!vv) return
    const update = () => {
      const hidden = window.innerHeight - vv.height - vv.offsetTop
      setInset(hidden > 1 ? Math.round(hidden) : 0)
    }
    update()
    vv.addEventListener("resize", update)
    vv.addEventListener("scroll", update)
    return () => {
      vv.removeEventListener("resize", update)
      vv.removeEventListener("scroll", update)
    }
  }, [])

  return inset
}
