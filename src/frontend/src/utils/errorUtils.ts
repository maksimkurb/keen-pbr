import { KeenPBRAPIError } from "../api/client";

/**
 * Formats an error for display to the user.
 * If the error is a KeenPBRAPIError with validation errors, formats them nicely.
 * Otherwise, returns the error message as-is.
 */
export function formatError(error: unknown): string {
	if (error instanceof KeenPBRAPIError) {
		const validationErrors = error.getValidationErrors();
		if (validationErrors && validationErrors.length > 0) {
			// Format validation errors as a list
			const errorList = validationErrors
				.map((ve) => {
					const prefix = ve.item ? `[${ve.item}] ` : "";
					return `• ${prefix}${ve.field}: ${ve.message}`;
				})
				.join("\n");

			return `${error.message}:\n${errorList}`;
		}
		return error.message;
	}

	if (error instanceof Error) {
		return error.message;
	}

	return String(error);
}
