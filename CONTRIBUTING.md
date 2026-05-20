# Contributing

Thank you for wanting to improve keen-pbr. This project runs on real routers, so contributions need to be specific, tested, and easy to review.

## Before Opening a Merge Request

Please create an issue before opening a merge request.

The issue should describe a concrete problem or a clearly scoped improvement. Include enough context for maintainers and other contributors to understand what is wrong, what should change, and how the change can be verified.

Merge requests without a linked issue may be rejected without review.

## Testing Requirements

Before submitting a merge request, test your changes on your own router or on a test device that is close to the real target environment.

In the merge request, include proof that the change works. Useful proof can include:

- logs
- screenshots
- command output
- configuration snippets
- a clear description of the router model, firmware, and test scenario

Code that was not tested by the merge request author will not be merged.

## AI-Generated Code

AI tools are welcome as assistants, but the merge request author is responsible for the result.

If you use AI-generated code, you must read it, understand it, and test it yourself in a real environment before submitting it.

Untested AI-generated code will be rejected. Maintainers do not have enough time to debug changes that the author has not personally verified.

## Build

Always build from the repository root with:

```sh
make
```

This runs the project CMake configuration and build through the root `Makefile`.

## Generated Files

Do not edit generated files by hand. Update the source schema or configuration and run the appropriate generation command instead.

For backend API types:

```sh
make generate
```

For the frontend API client and models:

```sh
make frontend-api-generate
```

## Frontend

The frontend lives in the `frontend/` directory.

Use `bun` and `bunx` for frontend package management and tooling. Do not use `npm`, `pnpm`, or `yarn` unless maintainers explicitly ask for it.

The frontend uses `base-ui` instead of `radix-ui`.

## Merge Request Checklist

Before submitting a merge request, make sure that:

- [ ] there is a linked issue describing the problem or improvement
- [ ] the project builds successfully
- [ ] the change was tested on a real router or representative test device
- [ ] logs, screenshots, command output, or other proof of testing are included
- [ ] all submitted code was read, understood, and verified by the author
- [ ] any AI-generated code was personally reviewed and tested by the author

## Review Expectations

Keep merge requests focused. Small, well-scoped changes are easier to review and safer to merge.

Avoid unrelated refactoring in feature or bugfix merge requests. If a cleanup is needed, open a separate issue or merge request for it.

Maintainers may ask for additional logs, reproduction steps, or router-specific testing before merging.
