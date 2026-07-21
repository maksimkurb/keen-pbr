#!/usr/bin/env python3
"""Small dependency-free case runner shared by the packaged-system suite."""

from __future__ import annotations

import dataclasses
import json
import os
import signal
import time
import traceback
from collections.abc import Callable, Iterable


Backend = str
CaseBody = Callable[[object], None]


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    backends: frozenset[Backend]
    body: CaseBody


class Registry:
    def __init__(self) -> None:
        self._cases: dict[str, Case] = {}

    def case(self, name: str, backends: Iterable[Backend] = ("iptables", "nftables")):
        def register(body: CaseBody) -> CaseBody:
            if not name or name in self._cases:
                raise ValueError(f"duplicate or empty integration case: {name!r}")
            supported = frozenset(backends)
            if not supported or not supported <= {"iptables", "nftables"}:
                raise ValueError(f"invalid backends for {name}: {sorted(supported)}")
            self._cases[name] = Case(name, supported, body)
            return body
        return register

    def select(self, requested: str, backend: Backend) -> list[Case]:
        available = [case for case in self._cases.values() if backend in case.backends]
        if requested.strip() in ("", "all"):
            return available
        names = [name.strip() for name in requested.split(",") if name.strip()]
        unknown = [name for name in names if name not in self._cases]
        unsupported = [name for name in names
                       if name in self._cases and backend not in self._cases[name].backends]
        if unknown:
            raise ValueError("unknown integration case(s): " + ", ".join(unknown))
        if unsupported:
            raise ValueError(f"case(s) unsupported by {backend}: " + ", ".join(unsupported))
        return [self._cases[name] for name in names]

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(self._cases)


def _field(value: object) -> str:
    return str(value).replace("\r", "\\r").replace("\n", "\\n").replace(" ", "_")


def marker(kind: str, **fields: object) -> str:
    ordered = " ".join(f"{key}={_field(value)}" for key, value in fields.items())
    return f"KPBR_IT_{kind}" + (f" {ordered}" if ordered else "")


def truncate_diagnostic(text: str, max_lines: int = 80, max_bytes: int = 16384) -> str:
    encoded = text.encode("utf-8", errors="replace")
    original_bytes = len(encoded)
    byte_cut = len(encoded) > max_bytes
    if byte_cut:
        encoded = encoded[:max_bytes]
        text = encoded.decode("utf-8", errors="replace")
    lines = text.splitlines()
    line_cut = len(lines) > max_lines
    kept = lines[:max_lines]
    if byte_cut or line_cut:
        kept.append(f"... diagnostic truncated (lines={len(lines)}, bytes={original_bytes})")
    return "\n".join(kept)


@dataclasses.dataclass
class Result:
    backend: Backend
    case: str
    status: str
    duration_ms: int
    error: str = ""


class Reporter:
    def __init__(self, emit: Callable[[str], None] = print) -> None:
        self.emit = emit

    def event(self, kind: str, **fields: object) -> None:
        self.emit(marker(kind, **fields))

    def diagnostic(self, backend: str, case: str, text: str) -> None:
        for line in truncate_diagnostic(text).splitlines():
            self.event("DIAG", backend=backend, case=case, message=line)


class Runner:
    def __init__(self, backend: Backend, cases: list[Case], context: object,
                 setup: Callable[[object, Case], None],
                 teardown: Callable[[object, Case], None],
                 diagnose: Callable[[object, Case], str],
                 reporter: Reporter | None = None,
                 timeout_seconds: float = 180.0,
                 preserve: Callable[[Case, str], None] | None = None) -> None:
        self.backend = backend
        self.cases = cases
        self.context = context
        self.setup = setup
        self.teardown = teardown
        self.diagnose = diagnose
        self.reporter = reporter or Reporter()
        self.timeout_seconds = timeout_seconds
        self.preserve = preserve

    def _diagnose(self, case: Case, prefix: str) -> None:
        try:
            detail = self.diagnose(self.context, case)
        except BaseException as exc:
            detail = f"diagnostic collection failed: {type(exc).__name__}: {exc}"
        raw = prefix + "\n" + detail
        if self.preserve is not None:
            try:
                self.preserve(case, raw)
            except BaseException as exc:
                raw += f"\nraw diagnostic preservation failed: {type(exc).__name__}: {exc}"
        self.reporter.diagnostic(self.backend, case.name, raw)

    def run(self) -> list[Result]:
        results: list[Result] = []
        self.reporter.event("BEGIN", backend=self.backend, cases=len(self.cases))
        for case in self.cases:
            started = time.monotonic()
            status = "pass"
            error = ""
            self.reporter.event("EVENT", backend=self.backend, case=case.name,
                                stage="setup", status="begin")
            try:
                self.setup(self.context, case)
                self.reporter.event("EVENT", backend=self.backend, case=case.name,
                                    stage="run", status="begin")
                previous_handler = signal.getsignal(signal.SIGALRM)
                signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(
                    TimeoutError(f"case exceeded {self.timeout_seconds:g} seconds")))
                signal.setitimer(signal.ITIMER_REAL, self.timeout_seconds)
                case.body(self.context)
            except BaseException as exc:  # teardown must also run after interrupts/timeouts
                status = "fail"
                error = f"{type(exc).__name__}: {exc}"
                self._diagnose(case, traceback.format_exc())
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
                if "previous_handler" in locals():
                    signal.signal(signal.SIGALRM, previous_handler)
                    del previous_handler
                try:
                    self.teardown(self.context, case)
                except BaseException as exc:
                    if status == "pass":
                        status = "fail"
                        error = f"teardown {type(exc).__name__}: {exc}"
                        self._diagnose(case, traceback.format_exc())
            duration_ms = int((time.monotonic() - started) * 1000)
            results.append(Result(self.backend, case.name, status, duration_ms, error))
            self.reporter.event("END", backend=self.backend, case=case.name,
                                status=status, duration_ms=duration_ms)
        return results


def aggregate_status(results: Iterable[Result]) -> int:
    return 1 if any(result.status != "pass" for result in results) else 0


def write_summary(path: str, backend: str, results: list[Result]) -> None:
    payload = {
        "backend": backend,
        "status": "fail" if aggregate_status(results) else "pass",
        "results": [dataclasses.asdict(result) for result in results],
    }
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)
