# keen-pbr UI User Stories

This document describes the current wireframed UI for `keen-pbr`.
The intended implementation target is the existing `frontend/` app:
React + TypeScript + Vite, using `shadcn/ui` and Base UI, with no server-side rendering.

The HTML wireframes that back this document live in this same folder:

- `index.html`
- `overview.html`
- `configuration.html`
- `config-general.html`
- `config-lists.html`
- `config-outbounds.html`
- `config-dns.html`
- `config-routing-rules.html`

## Product assumptions

- The UI talks to the backend using relative `/api/...` paths.
- Authentication and authorization are out of scope for v1.
- "Restart service" maps to reload via `POST /api/reload`.
- The UI uses the current API surface only:
  - `GET /api/health/service`
  - `GET /api/health/routing`
  - `GET /api/config`
  - `POST /api/config`
  - `POST /api/reload`
  - `POST /api/routing/test`
  - `GET /api/dns/test`
- Success/error notifications are handled with toast UI such as Sonner, not a dedicated action log widget.

## Page structure

### Overview

Wireframe: `overview.html`

The Overview page is the main operational dashboard. It includes:

- Daemon status and reload action
- Routing health summary
- DNS test widget
- Outbound status table with packet and transfer counters
- Unified routing rule health table
- Routing test widget

### Configuration hub

Wireframe: `configuration.html`

The Configuration page is now a hub page that links to focused config subpages instead
of showing the entire config in one long screen.

### General config

Wireframe: `config-general.html`

This page contains:

- Global strict enforcement
- Global lists autoupdate settings
- Advanced routing settings such as `fwmark` and `iproute`

### Lists

Wireframe: `config-lists.html`

This page contains:

- Lists table with last updated time
- Single-list create modal mock
- Single-list edit modal mock

### Outbounds

Wireframe: `config-outbounds.html`

This page contains:

- Outbounds table
- Single-outbound create modal mock
- Single-outbound edit modal mock

### DNS

Wireframe: `config-dns.html`

This page contains:

- DNS servers
- List-to-server rules
- Fallback server selection

### Routing rules

Wireframe: `config-routing-rules.html`

This page contains:

- Route rule list
- Ordering controls
- Rule field editing with select-based fields where appropriate

## User stories

### Epic 1: Operations overview

**Story 1.1: See daemon health**

As an operator, I want to see daemon version and running status so I can verify the
service is alive and the UI is connected to the correct instance.

Acceptance criteria:
- The Overview page loads service health from `GET /api/health/service`.
- Daemon version and status are shown in the top row.
- Reload is available from the daemon widget.

**Story 1.2: See routing health**

As an operator, I want a concise routing health widget so I can tell whether live kernel
state is healthy before reading detailed rule checks.

Acceptance criteria:
- The Overview page loads routing health from `GET /api/health/routing`.
- Overall routing status and firewall backend are shown in the top row.

**Story 1.3: Run DNS self-check**

As an operator, I want the UI to verify whether my current device is using the expected
DNS path so I can quickly detect local DNS misconfiguration.

Acceptance criteria:
- The DNS test widget lives on the Overview page top row.
- The component connects to `GET /api/dns/test` on render.
- It waits for the initial `HELLO` event.
- It triggers a fetch to `<randomstring>.test.keen.pbr` without authorization.
- It waits up to 5 seconds for the corresponding DNS event.
- On success it shows a healthy state with observed source IP and ECS.
- On failure it shows that the current device DNS appears misconfigured.
- The technical implementation details stay out of visible UI and may exist as code comments only.

**Story 1.4: See outbound status and traffic**

As an operator, I want to inspect outbounds with packet and transfer counts so I can
understand which routes are active and how much traffic they carry.

Acceptance criteria:
- The Outbounds table shows tag, type, health status, packet count, and transferred data.
- Transfer size is displayed in auto-scaled units such as KB, MB, GB, or TB.
- `urltest` rows also show the selected outbound and child state details.

**Story 1.5: See routing health grouped by config rule**

As an operator, I want one rule-centric table instead of separate backend-specific tables
so I can compare match conditions, routing state, and health in one place.

Acceptance criteria:
- Firewall rule checks, route tables, and policy rules are shown as one table grouped by config rule.
- Each row shows the match definition, outbound, fwmark/counters, route table info, policy rule info, and overall status.

**Story 1.6: Run routing tests**

As an operator, I want to test routing for a domain or IP from the Overview page so I do
not need to leave the main dashboard for a common troubleshooting action.

Acceptance criteria:
- The routing test widget lives on the Overview page.
- It submits to `POST /api/routing/test`.
- It shows resolved IPs, warnings, expected outbound, actual outbound, and whether each result matched.

### Epic 2: Configuration navigation

**Story 2.1: Switch between config areas quickly**

As an operator, I want every wireframed page to expose all major pages in the left menu
so I can jump directly between Overview and config sections.

Acceptance criteria:
- The left menu includes Overview, Configuration, General config, Lists, Outbounds, DNS, and Routing rules.
- This menu is visible on all wireframe pages.

**Story 2.2: Use a config hub**

As an operator, I want a configuration hub page so I can see the available configuration
areas before drilling into a specific subsystem.

Acceptance criteria:
- `configuration.html` links to the five dedicated config pages.
- The hub does not try to render all config sections inline.

### Epic 3: General configuration

**Story 3.1: Edit daemon defaults**

As an operator, I want to edit only the daemon-level settings that matter in the UI so
the screen stays focused and does not expose unnecessary filesystem or API internals.

Acceptance criteria:
- General config exposes global strict enforcement.
- PID path, cache path, API enabled, and API listen address are not exposed in the UI.

**Story 3.2: Edit global list refresh**

As an operator, I want list autoupdate settings in a single global place so I do not
mistakenly treat them as per-list settings.

Acceptance criteria:
- Global `lists_autoupdate.enabled` and `lists_autoupdate.cron` live on the General config page.
- Lists pages do not present autoupdate as a per-list setting.

**Story 3.3: Edit advanced routing values safely**

As an operator, I want clear helpers around fwmark and route-table settings so I can
enter advanced values correctly.

Acceptance criteria:
- `fwmark.start` and `fwmark.mask` are shown as hexadecimal values.
- `iproute.table_start` is editable on the same page.
- The spec notes that `fwmark.mask` uses a specialized React input that blocks invalid characters and invalid mask shapes during typing.

### Epic 4: Lists management

**Story 4.1: See lists inventory**

As an operator, I want a table of configured lists so I can understand their source,
TTL, and last refresh time at a glance.

Acceptance criteria:
- The Lists page shows name, source, TTL, and last updated time.
- Last updated reflects cache file modification time for the list.

**Story 4.2: Create and edit one list at a time**

As an operator, I want list create/edit to happen in a dedicated page area or modal so I
can focus on one list without scanning unrelated config sections.

Acceptance criteria:
- The Lists page shows single-list create and edit mock states.
- Each list supports `url`, `file`, `domains`, `ip_cidrs`, and `ttl_ms`.
- Domains field includes guidance that entering `example.com` also includes subdomains.
- IP CIDR examples include IPv4 and IPv6.

### Epic 5: Outbounds management

**Story 5.1: See outbound inventory**

As an operator, I want a table of outbounds so I can review configured targets before
editing them.

Acceptance criteria:
- The Outbounds page shows a list of outbounds above the edit/create area.
- Each row shows tag, type, summary details, and actions.

**Story 5.2: Create one outbound at a time**

As an operator, I want outbound creation to start with tag and type only so the UI can
branch into the right fields after I choose the outbound kind.

Acceptance criteria:
- Create flow starts with tag and type.
- Continue reveals type-specific fields.
- Tag becomes read-only after creation.

**Story 5.3: Edit urltest outbounds**

As an operator, I want the urltest editor laid out in the order I think about it:
selected outbounds first, then probing behavior, then retries and recovery.

Acceptance criteria:
- In the edit mock, outbound groups are shown above probing settings.
- Probing includes Probe URL, Interval, and Tolerance with hint text.
- Retry on fail includes Attempts and Interval with hint text.
- Circuit breaker fields include human-friendly descriptions.
- Outbounds inside a group are selected through combobox/select-style controls.
- Strict enforcement is available as `Default (as in global config)`, `Enabled`, or `Disabled`.

### Epic 6: DNS management

**Story 6.1: Configure DNS servers and fallback**

As an operator, I want to manage DNS servers and fallback routing so I can control how
domains are resolved.

Acceptance criteria:
- The DNS page includes server rows with `tag`, `address`, and optional `detour`.
- Fallback server is configured separately.

**Story 6.2: Map one list to one DNS server rule**

As an operator, I want each DNS rule to represent a single list-to-server mapping so the
rule table stays explicit and easy to audit.

Acceptance criteria:
- DNS rules are rendered one row per list.
- Rule fields use select controls for list and server tag.

### Epic 7: Routing rules management

**Story 7.1: Edit route rules with constrained choices**

As an operator, I want route rules to use select-based choices where possible so I can
avoid invalid combinations and typo-driven errors.

Acceptance criteria:
- The Routing rules page shows a list/table of rules.
- `list`, `outbound`, and `proto` use select controls.
- `proto` options are `tcp/udp`, `tcp`, `udp`, and `any`.
- Address and port match fields remain editable text inputs.
- Ordering controls are visible in each row.

## Non-goals for v1

- Multi-user collaboration
- Auth, roles, or permissions
- Server-side rendering
- Backend API additions beyond the current OpenAPI document
- A standalone diagnostics page for common day-to-day checks

## Release checklist for implementation

- Wireframes and implementation stay aligned with the pages listed in this folder.
- The left menu contains all main pages and config subpages.
- Overview remains the home for daemon health, routing health, DNS test, outbound health, and routing test.
- Configuration remains split into dedicated subsystem pages rather than one long form page.
