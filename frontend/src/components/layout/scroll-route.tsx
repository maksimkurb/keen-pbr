import { useEffect } from "react"
import { useLocation } from "wouter"

export function ScrollToTopOnRouteChange() {
  const [pathname] = useLocation()

  useEffect(() => {
    window.scrollTo({
      top: 0,
      left: 0,
      behavior: "auto",
    })
  }, [pathname])

  return null
}
