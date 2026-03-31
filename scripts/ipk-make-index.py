#!/usr/bin/env python3

import gzip
import hashlib
import io
import os
import sys
import tarfile
from collections import OrderedDict
from pathlib import Path

WRONG_FIELDS = {
    "Maintainer",
    "LicenseFiles",
    "Source",
    "SourceName",
    "Require",
    "SourceDateEpoch",
}


def file_sha256(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def normalize_fields(control_text: str) -> OrderedDict[str, str]:
    out: OrderedDict[str, str] = OrderedDict()
    for line in control_text.splitlines():
        if ": " not in line:
            continue
        field, data = line.split(": ", maxsplit=1)
        if field in WRONG_FIELDS:
            continue
        out[field] = data
    return out


def read_control_from_ipk(ipk_path: Path) -> str:
    with tarfile.open(ipk_path, "r:gz") as outer_tar:
        control_blob = None
        for member_name in ("./control.tar.gz", "control.tar.gz"):
            try:
                extracted = outer_tar.extractfile(member_name)
            except KeyError:
                extracted = None
            if extracted is not None:
                control_blob = extracted.read()
                break
        if control_blob is None:
            raise RuntimeError(f"{ipk_path.name}: control.tar.gz not found")

    with tarfile.open(fileobj=io.BytesIO(control_blob), mode="r:gz") as control_tar:
        control_file = None
        for member_name in ("./control", "control"):
            try:
                extracted = control_tar.extractfile(member_name)
            except KeyError:
                extracted = None
            if extracted is not None:
                control_file = extracted
                break
        if control_file is None:
            raise RuntimeError(f"{ipk_path.name}: control file not found")
        return control_file.read().decode("utf-8", errors="replace")


def split_description(control_text: str) -> tuple[str, str]:
    marker = "Description"
    idx = control_text.find(marker)
    if idx == -1:
        return control_text.strip(), "Description:  "
    header = control_text[: max(idx - 1, 0)].strip()
    description = control_text[idx:].strip()
    return header, description


def generate(path: Path) -> None:
    if not path.is_dir():
        raise RuntimeError(f"{path} folder does not exist")

    packages_manifest = path / "Packages.manifest"
    packages_index = path / "Packages"

    ipk_files = sorted([p for p in path.iterdir() if p.is_file() and p.suffix == ".ipk"], key=lambda p: p.name.split("_")[0])

    with packages_manifest.open("w", encoding="utf-8") as manifest_fh, packages_index.open("w", encoding="utf-8") as packages_fh:
        for ipk_path in ipk_files:
            control_text = read_control_from_ipk(ipk_path)
            control_no_desc, description = split_description(control_text)

            manifest_fh.write(control_no_desc)
            fields = normalize_fields(control_no_desc)
            fields["Filename"] = ipk_path.name
            fields["Size"] = str(ipk_path.stat().st_size)
            fields["SHA256sum"] = file_sha256(ipk_path)
            for key in ("Filename", "Size", "SHA256sum"):
                manifest_fh.write(f"\n{key}: {fields[key]}")
            manifest_fh.write(f"\n{description}\n\n")

            packages_fh.write("\n".join([f"{k}: {v}" for k, v in fields.items()]))
            packages_fh.write(f"\n{description}\n\n")

    with gzip.open(path / "Packages.gz", "wb") as gz_fh, packages_index.open("rb") as pkg_fh:
        gz_fh.write(pkg_fh.read())


if __name__ == "__main__":
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    generate(target.resolve())
