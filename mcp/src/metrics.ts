export interface MetricSample {
  name: string;
  labels?: Record<string, string>;
  value: number;
}

export interface MetricFamily {
  name: string;
  type?: string;
  help?: string;
  samples: MetricSample[];
}

export interface MetricsSummary {
  family_count: number;
  sample_count: number;
  parse_warnings: number;
  build?: Record<string, string>;
  highlights: {
    active_rules?: number;
    config_reload_errors_total?: number;
    list_update_errors: MetricSample[];
    errors_total_nonzero: MetricSample[];
    probes_down: MetricSample[];
    dns_upstreams_down: MetricSample[];
  };
  filter?: {
    query: string;
    matched_families: string[];
    samples: MetricSample[];
    truncated: boolean;
  };
}

const SAMPLE_RE = /^([a-zA-Z_:][a-zA-Z0-9_:]*)(?:\{(.*)\})?\s+(-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|NaN|[+-]Inf)\s*(?:\d+)?$/;
const LABEL_RE = /([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*"((?:[^"\\]|\\.)*)"/g;

/**
 * Parses Prometheus text exposition data into families. This is deliberately
 * small: keen-pbr metrics are simple counters, gauges and histograms.
 */
export function parseMetrics(text: string): { families: MetricFamily[]; parseWarnings: number } {
  const families = new Map<string, MetricFamily>();
  let parseWarnings = 0;

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line) continue;
    if (line.startsWith("# HELP ")) {
      const rest = line.slice("# HELP ".length);
      const space = rest.indexOf(" ");
      if (space <= 0) continue;
      const family = ensureFamily(families, rest.slice(0, space));
      family.help ??= rest.slice(space + 1);
      continue;
    }
    if (line.startsWith("# TYPE ")) {
      const rest = line.slice("# TYPE ".length);
      const space = rest.indexOf(" ");
      if (space <= 0) continue;
      const family = ensureFamily(families, rest.slice(0, space));
      family.type ??= rest.slice(space + 1).trim();
      continue;
    }
    if (line.startsWith("#")) continue;

    const match = SAMPLE_RE.exec(line);
    if (!match) {
      parseWarnings += 1;
      continue;
    }
    const [, name, labelsRaw, valueRaw] = match;
    if (!name || !valueRaw) {
      parseWarnings += 1;
      continue;
    }
    const value = Number(valueRaw);
    const sample: MetricSample = {
      name,
      ...(labelsRaw ? { labels: parseLabels(labelsRaw) } : {}),
      value: Number.isNaN(value) ? Number.NaN : value,
    };
    ensureFamily(families, name).samples.push(sample);
  }

  return { families: [...families.values()], parseWarnings };
}

export function summarizeMetrics(
  text: string,
  options: { filter?: string; maxSamples?: number } = {},
): MetricsSummary {
  const { families, parseWarnings } = parseMetrics(text);
  const maxSamples = Math.min(Math.max(options.maxSamples ?? 200, 1), 2_000);

  const listUpdateErrors = samplesOf(families, "keen_pbr_list_update_errors_total").filter((sample) => sample.value > 0);
  const errorsTotalNonzero = samplesOf(families, "keen_pbr_errors_total").filter((sample) => sample.value > 0);
  const probesDown = samplesOf(families, "keen_pbr_probe_up").filter((sample) => sample.value === 0);
  const dnsUpstreamsDown = samplesOf(families, "keen_pbr_dns_proxy_upstream_up").filter(
    (sample) => sample.value === 0,
  );

  const buildSample = samplesOf(families, "keen_pbr_build_info")[0];
  const activeRules = valueOf(families, "keen_pbr_active_rules");
  const reloadErrors = valueOf(families, "keen_pbr_config_reload_errors_total");

  const summary: MetricsSummary = {
    family_count: families.length,
    sample_count: families.reduce((total, family) => total + family.samples.length, 0),
    parse_warnings: parseWarnings,
    ...(buildSample?.labels ? { build: buildSample.labels } : {}),
    highlights: {
      ...(activeRules === undefined ? {} : { active_rules: activeRules }),
      ...(reloadErrors === undefined ? {} : { config_reload_errors_total: reloadErrors }),
      list_update_errors: listUpdateErrors.slice(0, 20),
      errors_total_nonzero: errorsTotalNonzero.slice(0, 20),
      probes_down: probesDown.slice(0, 20),
      dns_upstreams_down: dnsUpstreamsDown.slice(0, 20),
    },
  };

  const filter = options.filter?.trim();
  if (filter) {
    const needle = filter.toLowerCase();
    const matchedFamilies = families.filter((family) => family.name.toLowerCase().includes(needle));
    const samples: MetricSample[] = [];
    let truncated = false;
    for (const family of matchedFamilies) {
      for (const sample of family.samples) {
        if (samples.length >= maxSamples) {
          truncated = true;
          break;
        }
        samples.push(sample);
      }
      if (truncated) break;
    }
    summary.filter = {
      query: filter,
      matched_families: matchedFamilies.map((family) => family.name),
      samples,
      truncated,
    };
  }

  return summary;
}

function ensureFamily(families: Map<string, MetricFamily>, name: string): MetricFamily {
  let family = families.get(name);
  if (!family) {
    family = { name, samples: [] };
    families.set(name, family);
  }
  return family;
}

function samplesOf(families: MetricFamily[], name: string): MetricSample[] {
  return families.find((family) => family.name === name)?.samples ?? [];
}

function valueOf(families: MetricFamily[], name: string): number | undefined {
  const sample = samplesOf(families, name)[0];
  return sample?.value;
}

function parseLabels(raw: string): Record<string, string> {
  const labels: Record<string, string> = {};
  for (const match of raw.matchAll(LABEL_RE)) {
    const key = match[1];
    const value = match[2];
    if (key !== undefined && value !== undefined) {
      labels[key] = value.replace(/\\(.)/g, "$1");
    }
  }
  return labels;
}
