import { useEffect } from "react"
import { useLocation } from "wouter"

export function ScrollToTopOnRouteChange() {
  const [pathname] = useLocation()

  useEffect(() => {
    const hash = window.location.hash.slice(1)
    if (hash) {
      let observer: MutationObserver | undefined
      let highlight: Animation | undefined
      const scrollToTarget = () => {
        const target = document.getElementById(hash)
        if (!target) {
          return false
        }
        highlight = scrollAndHighlightRouteTarget(target)
        observer?.disconnect()
        return true
      }
      const frame = requestAnimationFrame(() => {
        if (scrollToTarget()) {
          return
        }
        observer = new MutationObserver(scrollToTarget)
        observer.observe(document.body, { childList: true, subtree: true })
      })
      return () => {
        cancelAnimationFrame(frame)
        observer?.disconnect()
        highlight?.cancel()
      }
    }

    window.scrollTo({
      top: 0,
      left: 0,
      behavior: "auto",
    })
  }, [pathname])

  return null
}

// eslint-disable-next-line react-refresh/only-export-components
export function scrollAndHighlightRouteTarget(target: HTMLElement) {
  const field = target.closest<HTMLElement>('[data-slot="field"]') ?? target
  field.scrollIntoView({ behavior: "auto", block: "center" })
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    return undefined
  }
  return field.animate(
    [
      {
        backgroundColor: "transparent",
        boxShadow: "0 0 0 16px transparent",
        borderRadius: "8px",
        clipPath: "inset(-8px -16px round 16px)",
      },
      {
        backgroundColor: "rgba(245, 158, 11, 0.25)",
        boxShadow: "0 0 0 16px rgba(245, 158, 11, 0.25)",
        borderRadius: "8px",
        clipPath: "inset(-8px -16px round 16px)",
      },
      {
        backgroundColor: "transparent",
        boxShadow: "0 0 0 16px transparent",
        borderRadius: "8px",
        clipPath: "inset(-8px -16px round 16px)",
      },
    ],
    { duration: 900, iterations: 3, easing: "ease-in-out" }
  )
}
