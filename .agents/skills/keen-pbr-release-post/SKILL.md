---
name: keen-pbr-release-post
description: Draft keen-pbr release posts for GitHub and Telegram from git history. Use when asked to create or prepare keen-pbr release notes, GitHub release text, Telegram release announcements, changelog summaries, or when the user mentions keen_pbr_release_post. The skill analyzes commits and touched files since the latest semver release tag, asks for approval of the change list, then drafts English GitHub notes and a Russian Telegram post. It drafts only and never publishes.
---

# Keen PBR Release Post

## Overview

Use this skill to prepare release-post drafts for the keen-pbr project from local git history. Always separate analysis from writing: first propose a change list for user approval, then draft the GitHub and Telegram posts only after the user approves or edits that list.

## Workflow

1. Confirm you are in the keen-pbr repository.
   - Prefer the current workspace if it contains `.git` and project files.
   - If unclear, inspect `git remote -v`, `git status --short`, and nearby files before asking the user.

2. Collect release context.
   - Prefer `scripts/collect_release_context.sh` from this skill:
     ```sh
     /path/to/keen-pbr-release-post/scripts/collect_release_context.sh
     ```
   - If not using the script, run equivalent read-only commands:
     ```sh
     git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' HEAD
     git rev-parse --short HEAD
     git log --oneline <base-tag>..HEAD
     git diff --name-status <base-tag>..HEAD
     ```
   - Treat the base tag as the latest reachable `vX.Y.Z` semver tag. Ignore non-release tags such as `continuous`.

3. Analyze commits and touched files together.
   - Use commit names for intent, but verify scope from changed paths.
   - Group changes by product area:
     - Core
     - Web UI
     - OpenWrt & Repository
     - Keenetic OS
     - Documentation
     - CI/Build
     - Packaging
   - Merge noisy fixups and repeated CI attempts into one user-facing change.
   - Prefer user impact over implementation trivia. Mention internal details only when they explain a bug, migration, compatibility change, or risk.

4. Produce an approval checkpoint before writing posts.
   - Output only:
     - detected range and target version if known,
     - grouped proposed change list,
     - unclear items or suggested omissions.
   - Ask the user to approve or edit the list.
   - Do not draft GitHub or Telegram posts yet.

5. After approval or edits, draft both posts.
   - Read `references/release-style.md` before drafting.
   - GitHub post: English Markdown, concise sections, grouped bullets, full changelog compare link.
   - Telegram post: Russian announcement style, version header, numbered highlights, relevant warnings and upgrade notes, docs links, full changelog link.
   - Preserve user-approved facts. Do not invent release blockers, breaking changes, warnings, or metrics.

## Output Rules

- Never publish to GitHub, Telegram, or any external service.
- Never create release tags or commits.
- Do not edit repository files while using this skill unless the user separately asks to save drafts.
- If the target release version is not obvious from commits or project files, ask the user for it before final drafting.
- If there are no meaningful user-facing changes, say so and suggest omitting the release announcement.
