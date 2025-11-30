import type { ValidationErrorDetail } from '../api/client';

/**
 * Maps validation errors from API to a field-name-keyed object.
 * This makes it easy to look up errors by field name.
 *
 * Example:
 * Input: [{ field: "routing.interfaces", message: "must specify at least one interface" }]
 * Output: { "routing.interfaces": "must specify at least one interface" }
 */
export function mapValidationErrors(
  errors: ValidationErrorDetail[] | null | undefined,
): Record<string, string> {
  if (!errors || errors.length === 0) {
    return {};
  }

  const errorMap: Record<string, string> = {};
  for (const error of errors) {
    errorMap[error.field] = error.message;
  }
  return errorMap;
}

/**
 * Gets the error message for a specific field from the error map.
 * Supports nested field paths (e.g., "routing.interfaces").
 */
export function getFieldError(
  fieldName: string,
  errorMap: Record<string, string>,
): string | undefined {
  return errorMap[fieldName];
}
