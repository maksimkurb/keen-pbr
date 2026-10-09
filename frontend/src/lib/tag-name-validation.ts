export const TAG_NAME_PATTERN = /^[a-z][a-z0-9_]{0,23}$/

type TagNameValidationOptions = {
  duplicateError?: string | null
  invalidError: string
  requiredError: string
}

export function getTagNameValidationError(
  value: string,
  options: TagNameValidationOptions
) {
  const normalizedValue = value.trim()

  if (!normalizedValue) {
    return options.requiredError
  }

  if (!TAG_NAME_PATTERN.test(normalizedValue)) {
    return options.invalidError
  }

  return options.duplicateError ?? undefined
}
