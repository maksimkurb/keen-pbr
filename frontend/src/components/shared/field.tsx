import { cva, type VariantProps } from "class-variance-authority"
import { useMemo, type ReactNode } from "react"

import { Label } from "@/components/ui/label"
import { cn } from "@/lib/utils"

function FieldGroup({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      className={cn("group/field-group flex w-full flex-col gap-6", className)}
      data-slot="field-group"
      {...props}
    />
  )
}

const fieldVariants = cva("group/field flex w-full gap-3", {
  variants: {
    orientation: {
      vertical: "flex-col",
      horizontal:
        "flex-row items-start [&>[data-slot=field-label]]:flex-auto [&>[data-slot=field-label]]:pt-0.5",
    },
  },
  defaultVariants: {
    orientation: "vertical",
  },
})

function Field({
  className,
  orientation = "vertical",
  invalid = false,
  ...props
}: React.ComponentProps<"div"> &
  VariantProps<typeof fieldVariants> & { invalid?: boolean }) {
  return (
    <div
      className={cn(fieldVariants({ orientation }), className)}
      data-invalid={invalid}
      data-slot="field"
      role="group"
      {...props}
    />
  )
}

function FieldLabel({
  className,
  ...props
}: React.ComponentProps<typeof Label>) {
  return (
    <Label
      className={cn("flex w-fit items-center gap-2 text-base font-medium md:text-sm", className)}
      data-slot="field-label"
      {...props}
    />
  )
}

function FieldContent({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      className={cn("flex flex-1 flex-col gap-1.5", className)}
      data-slot="field-content"
      {...props}
    />
  )
}

function FieldDescription({
  className,
  ...props
}: React.ComponentProps<"div">) {
  return (
    <div
      className={cn("text-sm leading-normal text-muted-foreground md:text-xs", className)}
      data-slot="field-description"
      {...props}
    />
  )
}

function FieldSeparator({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      className={cn("h-px w-full bg-border", className)}
      data-slot="field-separator"
      {...props}
    />
  )
}

function FieldError({
  className,
  children,
  errors,
  ...props
}: React.ComponentProps<"div"> & {
  errors?: Array<{ message?: string } | undefined>
}) {
  const content = useMemo(() => {
    if (children) {
      return children
    }

    if (!errors?.length) {
      return null
    }

    const uniqueErrors = [
      ...new Map(errors.map((error) => [error?.message, error])).values(),
    ]

    if (uniqueErrors.length === 1) {
      return uniqueErrors[0]?.message
    }

    return (
      <ul className="ml-4 list-disc space-y-1">
        {uniqueErrors.map(
          (error, index) => error?.message && <li key={index}>{error.message}</li>
        )}
      </ul>
    )
  }, [children, errors])

  if (!content) {
    return null
  }

  return (
    <div
      className={cn("text-sm font-normal text-destructive md:text-xs", className)}
      data-slot="field-error"
      role="alert"
      {...props}
    >
      {content}
    </div>
  )
}

function FieldTitle({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      className={cn("flex items-center gap-2 text-base font-medium md:text-sm", className)}
      data-slot="field-title"
      {...props}
    />
  )
}

function FieldHint({
  description,
  error,
}: {
  description?: ReactNode
  error?: ReactNode
}) {
  if (error) {
    return <FieldError>{error}</FieldError>
  }

  if (!description) {
    return null
  }

  return <FieldDescription>{description}</FieldDescription>
}

export {
  Field,
  FieldContent,
  FieldDescription,
  FieldError,
  FieldGroup,
  FieldHint,
  FieldLabel,
  FieldSeparator,
  FieldTitle,
}
