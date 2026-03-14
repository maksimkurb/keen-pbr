import { Link, useLocation } from "wouter"

import { navItems } from "@/lib/routes"
import { Button, buttonVariants } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function TopNav() {
  const [location] = useLocation()

  return (
    <header className="border-b bg-white">
      <div className="mx-auto flex h-16 max-w-[1260px] items-center justify-between px-4">
        <div className="flex items-center gap-6">
          <div className="flex items-center gap-2">
            <div className="rounded bg-slate-800 px-2 py-1 text-sm font-bold text-white">kp</div>
            <span className="text-2xl font-semibold">keen-pbr</span>
          </div>
          <nav className="flex items-center gap-2">
            {navItems.map((item) => {
              const isActive = location === item.path
              return (
                <Link
                  className={cn(
                    buttonVariants({ variant: isActive ? "default" : "ghost", size: "default" }),
                    "px-3"
                  )}
                  href={item.path}
                  key={item.key}
                >
                  {item.label}
                </Link>
              )
            })}
          </nav>
        </div>
        <Button variant="outline">Русский</Button>
      </div>
    </header>
  )
}
