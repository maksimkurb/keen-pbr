# Agent Notes

## Build

Always build using the root `Makefile`:

```sh
make
```

This runs `cmake -S . -B cmake-build ...` followed by `cmake --build cmake-build`.

## Frontend

Frontend is lives in the `frontend/` folder. 
Always use bun/bunx as a package manager.
We are using base-ui instead of radix-ui.
