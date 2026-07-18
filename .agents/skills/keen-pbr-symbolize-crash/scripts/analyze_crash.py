#!/usr/bin/env python3
"""Prepare and conservatively symbolize a historical Keen PBR crash."""

from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from urllib.parse import quote


PT_LOAD = 1
ET_EXEC = 2
ET_DYN = 3
EM_AARCH64 = 183
RETURN_ADDRESS_ADJUSTMENTS = {
    3: (1,),       # i386
    8: (8, 4),     # MIPS, including delay-slot toolchains
    20: (4,),      # PPC32
    21: (4,),      # PPC64
    40: (4,),      # ARM
    62: (1,),      # x86_64
    183: (4,),     # AArch64
    243: (2, 4),   # RISC-V (compressed or regular instruction)
    258: (4,),     # LoongArch
}
BUILD_RE = re.compile(r"^\d{14}$")
MAP_RE = re.compile(
    r"^([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
    r"\S+\s+\d+\s*(.*)$"
)


class AnalysisError(RuntimeError):
    pass


def command(argv: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AnalysisError(f"command failed ({' '.join(argv)}): {detail}")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--branch")
    parser.add_argument("--channel", help="artifact channel; defaults to --branch")
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--build")
    parser.add_argument("--platform", choices=("openwrt", "keenetic"))
    parser.add_argument("--openwrt", help="legacy alias for OpenWrt target version")
    parser.add_argument("--target-version")
    parser.add_argument("--arch")
    parser.add_argument("--variant", choices=("full", "headless"))
    log_group = parser.add_mutually_exclusive_group(required=True)
    log_group.add_argument("--log-file", type=Path)
    log_group.add_argument("--log-text")
    parser.add_argument("--maps-file", type=Path)
    parser.add_argument("--debug-file", type=Path)
    parser.add_argument("--address", action="append", default=[])
    parser.add_argument("--load-bias", type=lambda value: int(value, 0))
    parser.add_argument(
        "--debug-base-url",
        default="https://raw.githubusercontent.com/maksimkurb/keen-pbr-repository/main",
    )
    parser.add_argument("--output-root", type=Path)
    return parser.parse_args()


def build_epoch(build: str) -> tuple[int, str]:
    if not BUILD_RE.fullmatch(build):
        raise AnalysisError("--build must use YYYYmmddHHMMSS")
    parsed = dt.datetime.strptime(build, "%Y%m%d%H%M%S").replace(tzinfo=dt.timezone.utc)
    return int(parsed.timestamp()), parsed.isoformat().replace("+00:00", "Z")


def parse_kpbr_metadata(text: str) -> dict[str, str]:
    if "=== KPBR-CRASH v1 BEGIN ===" not in text:
        return {}
    for line in text.splitlines():
        if not line.startswith("meta "):
            continue
        fields: dict[str, str] = {}
        for item in line[5:].split():
            if "=" in item:
                key, value = item.split("=", 1)
                fields[key] = value
        return fields
    raise AnalysisError("KPBR-CRASH v1 report has no meta line")


def resolve_ref(repo: Path, branch: str, remote: str, fetch: bool) -> str:
    if fetch:
        command(["git", "fetch", "--no-tags", remote, branch], cwd=repo)
        return "FETCH_HEAD"

    candidates = (
        f"refs/remotes/{remote}/{branch}",
        f"refs/heads/{branch}",
        branch,
    )
    for candidate in candidates:
        result = command(
            ["git", "rev-parse", "--verify", "--quiet", f"{candidate}^{{commit}}"],
            cwd=repo,
            check=False,
        )
        if result.returncode == 0:
            return candidate
    raise AnalysisError(f"branch not found: {branch}; retry with --fetch")


def resolve_commit(repo: Path, ref: str, epoch: int) -> dict[str, str]:
    lower = dt.datetime.fromtimestamp(epoch - 86400, dt.timezone.utc).isoformat()
    upper = dt.datetime.fromtimestamp(epoch + 86400, dt.timezone.utc).isoformat()
    output = command(
        [
            "git",
            "log",
            ref,
            f"--since={lower}",
            f"--until={upper}",
            "--format=%H%x09%ct%x09%cI%x09%s",
        ],
        cwd=repo,
    ).stdout
    matches: list[dict[str, str]] = []
    for line in output.splitlines():
        fields = line.split("\t", 3)
        if len(fields) == 4 and int(fields[1]) == epoch:
            matches.append(
                {"sha": fields[0], "epoch": fields[1], "commit_time": fields[2], "subject": fields[3]}
            )
    if not matches:
        raise AnalysisError(f"no commit on {ref} has the exact UTC build timestamp")
    if len(matches) > 1:
        shas = ", ".join(item["sha"] for item in matches)
        raise AnalysisError(f"multiple commits match the build timestamp: {shas}")
    return matches[0]


def version_at_commit(repo: Path, sha: str) -> str | None:
    result = command(["git", "show", f"{sha}:version.mk"], cwd=repo, check=False)
    if result.returncode != 0:
        return None
    match = re.search(r"^KEEN_PBR_VERSION\s*=\s*(\S+)\s*$", result.stdout, re.MULTILINE)
    return match.group(1) if match else None


def create_worktree(repo: Path, sha: str, output_root: Path | None) -> tuple[Path, Path]:
    if output_root:
        root = output_root.resolve()
        root.mkdir(parents=True, exist_ok=False)
    else:
        root = Path(tempfile.mkdtemp(prefix="keen-pbr-crash-"))
    worktree = root / "source"
    command(["git", "worktree", "add", "--detach", str(worktree), sha], cwd=repo)
    return root, worktree


def artifact_name(args: argparse.Namespace) -> str:
    if args.platform == "keenetic":
        return (
            f"keen-pbr_{args.version}-{args.build}_keenetic_"
            f"{args.arch}_{args.variant}.debug"
        )
    return (
        f"keen-pbr_{args.version}-{args.build}_openwrt_{args.target_version}_"
        f"{args.arch}_{args.variant}.debug"
    )


def acquire_debug(args: argparse.Namespace, root: Path) -> tuple[Path, str | None]:
    if args.debug_file:
        path = args.debug_file.resolve()
        if not path.is_file():
            raise AnalysisError(f"debug file does not exist: {path}")
        return path, None

    channel = args.channel or args.branch
    name = artifact_name(args)
    if args.platform == "keenetic":
        package_arch = args.arch.split("-", 1)[0]
        relative = (
            f"keenetic-debug/{quote(args.target_version, safe='')}/"
            f"{quote(package_arch, safe='')}/{quote(name, safe='')}"
        )
    else:
        relative = (
            f"openwrt-debug/{quote(args.target_version, safe='')}/"
            f"{quote(name, safe='')}"
        )
    url = (
        f"{args.debug_base_url.rstrip('/')}/repository/{quote(channel, safe='')}/"
        f"{relative}"
    )
    path = root / name
    command(["curl", "-fL", "--retry", "3", "-o", str(path), url])
    return path, url


def read_elf(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 52 or header[:4] != b"\x7fELF":
            raise AnalysisError(f"not an ELF file: {path}")
        elf_class = header[4]
        data_encoding = header[5]
        endian = "<" if data_encoding == 1 else ">" if data_encoding == 2 else None
        if endian is None or elf_class not in (1, 2):
            raise AnalysisError("unsupported ELF class or byte order")
        e_type, e_machine = struct.unpack_from(endian + "HH", header, 16)
        if elf_class == 2:
            e_phoff = struct.unpack_from(endian + "Q", header, 32)[0]
            e_phentsize, e_phnum = struct.unpack_from(endian + "HH", header, 54)
        else:
            e_phoff = struct.unpack_from(endian + "I", header, 28)[0]
            e_phentsize, e_phnum = struct.unpack_from(endian + "HH", header, 42)

        segments: list[dict[str, int]] = []
        for index in range(e_phnum):
            stream.seek(e_phoff + index * e_phentsize)
            entry = stream.read(e_phentsize)
            if elf_class == 2:
                p_type = struct.unpack_from(endian + "I", entry, 0)[0]
                p_offset, p_vaddr = struct.unpack_from(endian + "QQ", entry, 8)
                p_filesz, p_memsz = struct.unpack_from(endian + "QQ", entry, 32)
            else:
                p_type, p_offset, p_vaddr = struct.unpack_from(endian + "III", entry, 0)
                p_filesz, p_memsz = struct.unpack_from(endian + "II", entry, 16)
            if p_type == PT_LOAD:
                segments.append(
                    {
                        "offset": p_offset,
                        "vaddr": p_vaddr,
                        "filesz": p_filesz,
                        "memsz": p_memsz,
                    }
                )
    return {
        "class": 64 if elf_class == 2 else 32,
        "type": {ET_EXEC: "ET_EXEC", ET_DYN: "ET_DYN"}.get(e_type, str(e_type)),
        "machine": e_machine,
        "load_segments": segments,
    }


def parse_log(text: str, extra: list[str]) -> list[dict[str, object]]:
    found: list[dict[str, object]] = []
    seen: set[tuple[str, int]] = set()
    has_v1_frames = re.search(r"^frame\s+index=\d+\s+kind=", text, re.MULTILINE) is not None

    def add(kind: str, label: str, value: str, return_address: bool = False) -> None:
        address = int(value, 16)
        key = (label, address)
        if key not in seen:
            seen.add(key)
            found.append(
                {
                    "kind": kind,
                    "label": label,
                    "runtime_address": address,
                    "is_return_address": return_address,
                }
            )

    for line in text.splitlines():
        frame_match = re.search(
            r"^frame\s+index=(\d+)\s+kind=(fault|return)\s+pc=(0x[0-9a-fA-F]+)",
            line,
        )
        if frame_match:
            kind = frame_match.group(2)
            add(
                "stack",
                f"frame[{frame_match.group(1)}]",
                frame_match.group(3),
                kind == "return",
            )
        if not has_v1_frames:
            stack_match = re.search(r"\bpc\[(\d+)\]:\s*(0x[0-9a-fA-F]+)", line)
            if stack_match:
                add("stack", f"pc[{stack_match.group(1)}]", stack_match.group(2), True)
            for label in ("pc", "rip", "eip"):
                match = re.search(rf"(?:^|\s){label}=(0x[0-9a-fA-F]+)", line)
                if match:
                    add("fault-pc", label, match.group(1))
            for label in ("x30", "lr", "ra"):
                match = re.search(rf"(?:^|\s){label}=(0x[0-9a-fA-F]+)", line)
                if match:
                    add("return-register", label, match.group(1), True)
    for index, value in enumerate(extra):
        add("manual", f"address[{index}]", hex(int(value, 0)))
    return found


def parse_maps_text(text: str) -> list[dict[str, object]]:
    mappings: list[dict[str, object]] = []
    for line in text.splitlines():
        match = MAP_RE.match(line)
        if match:
            mappings.append(
                {
                    "start": int(match.group(1), 16),
                    "end": int(match.group(2), 16),
                    "perms": match.group(3),
                    "offset": int(match.group(4), 16),
                    "path": match.group(5).strip(),
                }
            )
    return mappings


def parse_maps(path: Path | None, log_text: str) -> list[dict[str, object]]:
    mappings = parse_maps_text(log_text)
    if path is not None:
        mappings.extend(parse_maps_text(path.read_text(encoding="utf-8", errors="replace")))
    return mappings


def in_load_range(address: int, elf: dict[str, object]) -> bool:
    return any(
        segment["vaddr"] <= address < segment["vaddr"] + segment["memsz"]
        for segment in elf["load_segments"]  # type: ignore[index]
    )


def mapping_for(address: int, mappings: list[dict[str, object]]) -> dict[str, object] | None:
    return next((item for item in mappings if item["start"] <= address < item["end"]), None)


def derive_main_bias(mapping: dict[str, object], elf: dict[str, object]) -> int | None:
    page = 4096
    map_offset = int(mapping["offset"])
    for segment in elf["load_segments"]:  # type: ignore[index]
        if segment["offset"] // page * page == map_offset:
            return int(mapping["start"]) - segment["vaddr"] // page * page
    return None


def choose_addr2line(elf: dict[str, object]) -> str:
    candidates = []
    if elf["machine"] == EM_AARCH64:
        candidates.append("aarch64-linux-gnu-addr2line")
    candidates.extend(("addr2line", "llvm-addr2line"))
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise AnalysisError("addr2line not found")


def symbolize(tool: str, debug_file: Path, address: int) -> dict[str, object]:
    result = command([tool, "-C", "-f", "-i", "-e", str(debug_file), hex(address)])
    lines = result.stdout.rstrip().splitlines()
    frames = []
    for index in range(0, len(lines), 2):
        function = lines[index] if index < len(lines) else "??"
        location = lines[index + 1] if index + 1 < len(lines) else "??:0"
        frames.append({"function": function, "location": location})
    has_symbol = any(frame["function"] != "??" for frame in frames)
    has_line = any(
        not str(frame["location"]).startswith("??:")
        and re.search(r":[1-9]\d*(?::\d+)?$", str(frame["location"])) is not None
        for frame in frames
    )
    return {"frames": frames, "has_symbol": has_symbol, "has_source_line": has_line}


def analyze_addresses(
    addresses: list[dict[str, object]],
    elf: dict[str, object],
    debug_file: Path,
    maps: list[dict[str, object]],
    explicit_bias: int | None,
) -> list[dict[str, object]]:
    tool = choose_addr2line(elf)
    output = []
    for item in addresses:
        runtime = int(item["runtime_address"])
        result = dict(item)
        result["runtime_address"] = hex(runtime)
        mapping = mapping_for(runtime, maps)
        if mapping:
            result["mapping"] = {
                **mapping,
                "start": hex(int(mapping["start"])),
                "end": hex(int(mapping["end"])),
                "offset": hex(int(mapping["offset"])),
                "module_file_offset": hex(runtime - int(mapping["start"]) + int(mapping["offset"])),
            }

        candidates: list[tuple[str, int]] = []
        if in_load_range(runtime, elf):
            candidates.append(("direct-et-exec-range", runtime))
        elif explicit_bias is not None and in_load_range(runtime - explicit_bias, elf):
            candidates.append(("explicit-load-bias", runtime - explicit_bias))
            result["load_bias"] = hex(explicit_bias)
        elif mapping and Path(str(mapping["path"])).name == "keen-pbr":
            bias = derive_main_bias(mapping, elf)
            if bias is not None and in_load_range(runtime - bias, elf):
                candidates.append(("maps-derived-load-bias", runtime - bias))
                result["load_bias"] = hex(bias)

        if not candidates:
            result["status"] = "unresolved-outside-keen-pbr"
            result["reason"] = (
                "address is outside the debug ELF PT_LOAD ranges and no proven Keen PBR load bias is available"
            )
            output.append(result)
            continue

        method, normalized = candidates[0]
        result["normalization"] = method
        result["elf_address"] = hex(normalized)
        symbolized = symbolize(tool, debug_file, normalized)
        result["symbolization"] = symbolized
        if symbolized["has_source_line"]:
            result["status"] = "resolved-source-line"
        elif symbolized["has_symbol"]:
            result["status"] = "resolved-function-only"
        else:
            result["status"] = "unresolved-no-symbol"

        if bool(item["is_return_address"]):
            attempts = []
            for adjustment in RETURN_ADDRESS_ADJUSTMENTS.get(int(elf["machine"]), (1,)):
                if normalized < adjustment:
                    continue
                adjusted = symbolize(tool, debug_file, normalized - adjustment)
                attempts.append(
                    {"adjustment": adjustment, "elf_address": hex(normalized - adjustment), **adjusted}
                )
                if adjusted["has_source_line"]:
                    break
            if attempts:
                result["call_site"] = attempts[-1]
        output.append(result)
    return output


def readable_stacktrace(addresses: list[dict[str, object]]) -> list[str]:
    lines = []
    for index, item in enumerate(addresses):
        mapping = item.get("mapping") or {}
        module = Path(str(mapping.get("path", "unknown"))).name or "unknown"
        symbols = item.get("symbolization") or {}
        call_site = item.get("call_site") or {}
        if call_site.get("has_source_line"):
            symbols = call_site
        frames = symbols.get("frames") or []
        function = "??"
        location = "??:0"
        if frames:
            function = str(frames[0].get("function", "??"))
            location = str(frames[0].get("location", "??:0"))
        lines.append(
            f"#{index} {module}!{function} at {location} "
            f"[{item['runtime_address']}; {item.get('status', 'unresolved')}]"
        )
    return lines


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    if command(["git", "rev-parse", "--show-toplevel"], cwd=repo).stdout.strip() != str(repo):
        raise AnalysisError(f"--repo must be the repository root: {repo}")

    log_text = (
        args.log_file.read_text(encoding="utf-8", errors="replace")
        if args.log_file
        else args.log_text
    )
    metadata = parse_kpbr_metadata(log_text)
    args.version = args.version or metadata.get("version")
    args.build = args.build or metadata.get("build")
    args.branch = args.branch or metadata.get("branch")
    args.platform = args.platform or metadata.get("target") or ("openwrt" if args.openwrt else None)
    args.target_version = args.target_version or args.openwrt or metadata.get("os_version")
    args.arch = args.arch or metadata.get("arch")
    args.variant = args.variant or metadata.get("variant") or "full"
    required = {
        "version": args.version,
        "build": args.build,
        "branch": args.branch,
        "platform": args.platform,
        "target version": args.target_version,
        "architecture": args.arch,
    }
    missing = [name for name, value in required.items() if not value or value == "unknown"]
    if missing:
        raise AnalysisError("missing crash metadata: " + ", ".join(missing))
    if args.platform not in ("openwrt", "keenetic"):
        raise AnalysisError(f"unsupported artifact platform: {args.platform}")

    epoch, build_utc = build_epoch(args.build)
    ref = resolve_ref(repo, args.branch, args.remote, args.fetch)
    embedded_commit = metadata.get("commit")
    if embedded_commit and embedded_commit != "unknown":
        resolved = command(
            ["git", "rev-parse", "--verify", f"{embedded_commit}^{{commit}}"],
            cwd=repo,
            check=False,
        )
        if resolved.returncode != 0:
            raise AnalysisError(f"embedded commit is not available locally: {embedded_commit}")
        sha = resolved.stdout.strip()
        fields = command(
            ["git", "show", "-s", "--format=%H%x09%ct%x09%cI%x09%s", sha], cwd=repo
        ).stdout.strip().split("\t", 3)
        commit = {"sha": fields[0], "epoch": fields[1], "commit_time": fields[2], "subject": fields[3]}
        if int(commit["epoch"]) != epoch:
            raise AnalysisError("embedded commit timestamp does not match build ID")
        ancestry = command(
            ["git", "merge-base", "--is-ancestor", sha, ref], cwd=repo, check=False
        )
        if ancestry.returncode != 0:
            raise AnalysisError(f"embedded commit {sha} is not reachable from {args.branch}")
    else:
        commit = resolve_commit(repo, ref, epoch)
    commit_version = version_at_commit(repo, commit["sha"])
    if commit_version and commit_version != args.version:
        raise AnalysisError(
            f"version mismatch: input is {args.version}, but {commit['sha']} has {commit_version}"
        )

    root, worktree = create_worktree(repo, commit["sha"], args.output_root)
    try:
        debug_file, artifact_url = acquire_debug(args, root)
        elf = read_elf(debug_file)
        addresses = parse_log(log_text, args.address)
        if not addresses:
            raise AnalysisError("no instruction or return addresses found in the crash log")
        maps = parse_maps(args.maps_file, log_text)
        analyzed = analyze_addresses(addresses, elf, debug_file, maps, args.load_bias)
        handler_backtrace_missing = (
            "=== CRASH" in log_text
            and "registers (" in log_text
            and "stack pcs:" not in log_text
            and "Segmentation fault" in log_text
        )
        report = {
            "build": {"id": args.build, "utc": build_utc, "version": args.version},
            "branch": args.branch,
            "platform": args.platform,
            "target_version": args.target_version,
            "resolved_ref": ref,
            "commit": commit,
            "temporary_root": str(root),
            "worktree": str(worktree),
            "debug_file": str(debug_file),
            "artifact_url": artifact_url,
            "elf": {
                **elf,
                "load_segments": [
                    {key: hex(value) for key, value in segment.items()}
                    for segment in elf["load_segments"]
                ],
            },
            "addresses": analyzed,
            "stacktrace": readable_stacktrace(analyzed),
            "crash_handler_backtrace_missing": handler_backtrace_missing,
            "cleanup": f"git -C {repo} worktree remove {worktree}",
        }
        report_path = root / "analysis.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0
    except Exception:
        print(f"Temporary worktree retained for inspection: {worktree}", file=sys.stderr)
        raise


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AnalysisError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
