# keen-pbr Release Post Style

## GitHub Release Notes

- Write in English.
- Start with a short product reminder only when the release is major or the audience may need context.
- Use Markdown headings grouped by product area, usually:
  - Core
  - Web UI
  - OpenWrt & Repository
  - Keenetic OS
  - Documentation
  - CI/Build
  - Packaging
- Use concise bullets with subsystem labels when helpful, for example `[Core]`, `[UI]`, `[OpenWrt]`, `[Repository]`.
- Include "Why it matters" only for changes whose impact is not obvious.
- Include important migration or repository URL changes prominently.
- End with:
  ```md
  **Full Changelog**: https://github.com/maksimkurb/keen-pbr/compare/<base-tag>...<target-tag>
  ```
  If the target tag does not exist yet, use the intended target version in the URL only after the user confirms it.

## Telegram Post

- Write in Russian.
- Use an announcement header:
  ```text
  🔥 Релиз vX.Y.Z (https://github.com/maksimkurb/keen-pbr/releases/tag/vX.Y.Z) 🟨🟨
  ```
- For 3.x beta releases, keep the beta caution unless the user says the release is no longer beta:
  ```text
  ❗️ Это BETA-релиз полностью переписанной с нуля keen-pbr.

  Данная версия ещё не протестирована большим количеством людей, поэтому баги неизбежны. Не устанавливайте данную версию на критичную для вас инфраструктуру.
  ```
- Introduce changes with:
  ```text
  Что нового и исправленного в vX.Y.Z:
  ```
- Use numbered highlights with emoji and compact explanations.
- Use em-dash bullets under a numbered item for multiple related fixes.
- Mention upgrade commands when relevant:
  ```text
  opkg update
  opkg install keen-pbr
  ```
- Include warning blocks for OpenWrt repository URL changes, breaking config changes, known critical issues, or migration steps only when supported by the approved change list.
- End with useful links when relevant:
  - upgrade from 3.x instructions or commands
  - upgrade from 2.x guide
  - fresh installation docs
  - quick start docs
  - full changelog release URL

## Common Links

- Upgrade from 2.x: `https://keen-pbr.fyi/ru/docs/getting-started/upgrade-from-2x/`
- Installation: `https://keen-pbr.fyi/ru/docs/getting-started/installation/`
- Quick start: `https://keen-pbr.fyi/ru/docs/getting-started/quick-start/`
- Release URL: `https://github.com/maksimkurb/keen-pbr/releases/tag/vX.Y.Z`
- Compare URL: `https://github.com/maksimkurb/keen-pbr/compare/<base-tag>...<target-tag>`
