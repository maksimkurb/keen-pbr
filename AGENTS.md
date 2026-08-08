# Agent Notes

## Build

Always build using the root `Makefile`:

```sh
make
```

This runs `cmake -S . -B cmake-build ...` followed by `cmake --build cmake-build`.

## C++

When a multiplication produces a size or count for a wider integer type, make
an operand the destination type so the multiplication itself is evaluated in
that type. For example, use `std::size_t{16} * 1024U`, not `16 * 1024` or a cast
of the completed product.

## Generated Files

Never edit generated files by hand. Update the source schema/config and run the
appropriate codegen command instead.

- Backend API types (`src/api/generated/api_types.hpp`): run `make generate`.
- Frontend API client/models (`frontend/src/api/generated/`): run `make frontend-api-generate`.

## Frontend

Frontend is lives in the `frontend/` folder. 
Always use bun/bunx as a package manager.
We are using base-ui instead of radix-ui.

Do not run make to compile C++ code if it wasn't edited (e.g. you edited only frontend code or docs)

@RTK.md
