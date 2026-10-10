# Package repository layout

keen-pbr publishes package feeds to an rsync host.

Branch builds and release tags deliberately use different publication semantics:

- branch builds publish individual targets as soon as each target succeeds;
- release tags remain transactional and only promote `stable` after the complete release matrix succeeds.

## Public channels

The stable public paths remain symlinks:

```text
<target-root>/repository/main -> main_<build-number>.release
<target-root>/repository/<branch> -> <branch>_<build-number>.release
<target-root>/repository/stable -> stable_<tag>_<build-number>.release
```

Package URLs therefore stay stable even though immutable release directories are created for every commit/tag.

## Incremental branch release directory

A branch commit is bootstrapped before package jobs start:

```text
<target-root>/repository/<branch>_<build-number>.release/
├── index.html
├── README.md
├── .release.json
└── ARTIFACTS_WAL.jsonl
```

Package jobs then upload only the leaf they own:

```text
<target-root>/repository/<branch>_<build-number>.release/
├── openwrt/<openwrt-version>/<arch>/
├── openwrt-debug/<openwrt-version>/
├── keenetic/<keenetic-version>/<arch>/
├── keenetic-debug/<keenetic-version>/<arch>/
└── debian/<debian-version>/<arch>/
```

The first successfully published target promotes the branch symlink to the new immutable release directory. Later targets populate that same directory independently.

A generation check under a very small `flock` critical section prevents an older, slower workflow run from moving a branch symlink backwards.

## Artifact WAL

`ARTIFACTS_WAL.jsonl` is the discovery source for the repository HTML page. Every successfully uploaded package target appends one JSON object, for example:

```json
{"key":"openwrt:25.12.2:aarch64_cortex-a53","platform":"openwrt","version":"25.12.2","arch":"aarch64_cortex-a53","formats":["apk"],"generation":1791234567,"sha":"0123456789abcdef","run_id":12345,"run_attempt":1,"published_at":1791234999}
```

The WAL append is the only shared mutable operation between target publishers and is protected with `flock`. Package uploads do not share a lock.

The browser reads `.release.json` and `ARTIFACTS_WAL.jsonl`, deduplicates records by `key`, and chooses the newest record by generation/run metadata. This means the repository page does not need to be regenerated whenever another architecture finishes.

## Automatic and manual branch matrices

Normal branch/main/PR builds intentionally exercise only representative targets:

- Debian trixie amd64
- Keenetic mipsel
- OpenWrt 25.12.2 aarch64_cortex-a53

A manual `Build packages` workflow run with `build_scope=full` builds the full Debian and Keenetic matrices and uses the same OpenWrt matrix declared by the release workflow.

Branch targets still publish independently in manual full builds.

## Release tags

Release-tag builds keep the existing all-or-nothing behavior:

1. build the complete OpenWrt, Keenetic, and Debian matrices;
2. collect all artifacts;
3. publish the complete repository tree;
4. only then atomically update `stable`.

If any required release target fails, `stable` is not changed.

## Shared assets

Shared repository UI and signing-key files live outside per-commit release directories:

```text
<target-root>/assets/
<target-root>/keys/
<target-root>/index.html
<target-root>/.htaccess
```

The root page redirects to `/repository/stable/`.

## Version semantics

- `build-number`: source commit UTC Unix timestamp.
- `keenetic-version`: currently `current`.
- `openwrt-version`: explicit OpenWrt release line.
- `debian-version`: explicit Debian release line.

## Rsync deployment configuration

The publisher consumes these repository secrets or variables:

| Setting | Purpose |
| --- | --- |
| `RSYNC_HOST` | SSH hostname/IP |
| `RSYNC_USERNAME` | SSH account |
| `RSYNC_SSH_PRIVATE_KEY` | private SSH key |
| `RSYNC_TARGET_ROOT` | web repository root |
| `RSYNC_PORT` | SSH port, default `22` |

The remote host must provide `flock`, `ssh`, and rsync-compatible filesystem semantics. The deploy account needs permission to create release directories and atomically replace symlinks below `<RSYNC_TARGET_ROOT>/repository`.
