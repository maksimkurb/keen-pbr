import { ToolInputError } from "./errors.js";

export interface ServerConfig {
  baseUrl: string;
  token: string;
  allowWrite: boolean;
  timeoutMs: number;
}

const DEFAULT_TIMEOUT_MS = 15_000;

export function loadConfig(env: NodeJS.ProcessEnv = process.env): ServerConfig {
  const rawUrl = env.KEEN_PBR_URL?.trim();
  if (!rawUrl) {
    throw new ToolInputError(
      "KEEN_PBR_URL is required, for example http://192.168.1.1:12121 or http://192.168.1.1:8080",
    );
  }

  let parsed: URL;
  try {
    parsed = new URL(rawUrl);
  } catch {
    throw new ToolInputError(`KEEN_PBR_URL is not a valid URL: ${rawUrl}`);
  }
  if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
    throw new ToolInputError("KEEN_PBR_URL must use http:// or https://");
  }
  if (parsed.search || parsed.hash) {
    throw new ToolInputError("KEEN_PBR_URL must not contain a query string or fragment");
  }

  const token = env.KEEN_PBR_TOKEN?.trim();
  if (!token) {
    throw new ToolInputError(
      "KEEN_PBR_TOKEN is required: send a long-lived keen-pbr API token in Authorization: Bearer <token>",
    );
  }

  const baseUrl = `${parsed.origin}${parsed.pathname.replace(/\/+$/, "")}`;

  const timeoutRaw = env.KEEN_PBR_TIMEOUT_MS?.trim();
  let timeoutMs = DEFAULT_TIMEOUT_MS;
  if (timeoutRaw) {
    const parsedTimeout = Number(timeoutRaw);
    if (!Number.isInteger(parsedTimeout) || parsedTimeout < 1_000 || parsedTimeout > 120_000) {
      throw new ToolInputError("KEEN_PBR_TIMEOUT_MS must be an integer between 1000 and 120000");
    }
    timeoutMs = parsedTimeout;
  }

  return {
    baseUrl,
    token,
    allowWrite: env.KEEN_PBR_ALLOW_WRITE === "1",
    timeoutMs,
  };
}
