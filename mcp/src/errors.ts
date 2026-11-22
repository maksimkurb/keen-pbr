export class KeenPbrApiError extends Error {
  readonly status?: number;
  readonly validationErrors: Array<{ path?: string; message: string }>;

  constructor(
    message: string,
    options: {
      status?: number;
      validationErrors?: Array<{ path?: string; message: string }>;
      cause?: unknown;
    } = {},
  ) {
    super(message, options.cause === undefined ? undefined : { cause: options.cause });
    this.name = "KeenPbrApiError";
    this.status = options.status;
    this.validationErrors = options.validationErrors ?? [];
  }
}

/** Invalid arguments supplied by the model or a missing precondition. */
export class ToolInputError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ToolInputError";
  }
}

export function describeError(error: unknown): string {
  if (error instanceof KeenPbrApiError) {
    const parts = [error.message];
    if (error.validationErrors.length > 0) {
      const rendered = error.validationErrors
        .slice(0, 10)
        .map((issue) => (issue.path ? `${issue.path}: ${issue.message}` : issue.message));
      parts.push(`Validation errors:\n- ${rendered.join("\n- ")}`);
      if (error.validationErrors.length > 10) {
        parts.push(`... and ${error.validationErrors.length - 10} more validation errors`);
      }
    }
    return parts.join("\n");
  }
  if (error instanceof Error) return error.message;
  return String(error);
}
