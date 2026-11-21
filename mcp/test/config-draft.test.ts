import { describe, expect, it } from "vitest";
import { KeenPbrClient } from "../src/client.js";
import { loadConfig } from "../src/config.js";
import { requireWrite } from "../src/tools/common.js";
import {
  addDomainsToDraft,
  createListInDraft,
  removeDomainsFromDraft,
} from "../src/tools/lists.js";
import { baseConfig, configState, healthyHealth } from "./fixtures.js";
import { MockHttp } from "./mock-http.js";

function clientFor(mock: MockHttp): KeenPbrClient {
  return new KeenPbrClient(
    { baseUrl: "http://router.test:12121", token: "test-token", timeoutMs: 5_000 },
    mock.fetch,
  );
}

function draftServer(config = baseConfig()): MockHttp {
  return new MockHttp()
    .onJson("GET", "/api/config", 200, configState(config))
    .onJson("POST", "/api/config", 200, { status: "ok", message: "Config staged in memory" });
}

describe("add_domain_to_list read-modify-write", () => {
  it("creates a missing list, normalizes and de-duplicates domains", async () => {
    const mock = draftServer();
    const result = await addDomainsToDraft(clientFor(mock), {
      list: "new_list",
      domains: ["Example.COM", "*.Foo.Example", "example.com"],
      createIfMissing: true,
      save: false,
      saveWaitSeconds: 20,
    });

    expect(result.changed).toBe(true);
    expect(result.is_draft).toBe(true);
    expect(result.change_summary).toMatchObject({
      list: "new_list",
      created_list: true,
      added: ["example.com", "*.foo.example"],
      already_present: ["example.com"],
    });
    expect(result.changes?.map((change) => change.path)).toContain("lists.new_list");
    expect(result.next_step).toContain("save_config");
    expect(result.saved).toBeUndefined();

    const gets = mock.requestsFor("GET", "/api/config");
    const posts = mock.requestsFor("POST", "/api/config");
    expect(gets).toHaveLength(1);
    expect(posts).toHaveLength(1);
    expect(mock.requestsFor("POST", "/api/config/save")).toHaveLength(0);

    const body = posts[0]?.body as Record<string, unknown>;
    expect((body.lists as Record<string, { domains?: string[] }>).new_list?.domains).toEqual([
      "example.com",
      "*.foo.example",
    ]);
    // The whole document is staged, not a partial patch.
    expect(body.route).toEqual(baseConfig().route);
    expect(body.outbounds).toEqual(baseConfig().outbounds);

    expect(gets[0]?.headers.authorization).toBe("Bearer test-token");
    expect(posts[0]?.headers.authorization).toBe("Bearer test-token");
  });

  it("treats a wildcard and its base domain as the same entry and does not stage a no-op", async () => {
    const config = baseConfig();
    config.lists = { existing: { domains: ["*.example.com"] } };
    const mock = draftServer(config);

    const result = await addDomainsToDraft(clientFor(mock), {
      list: "existing",
      domains: ["example.com", "EXAMPLE.COM."],
      createIfMissing: true,
      save: false,
      saveWaitSeconds: 20,
    });

    expect(result.changed).toBe(false);
    expect(result.message).toContain("Nothing changed");
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(0);
  });

  it("refuses to create a list when create_if_missing is false", async () => {
    const mock = draftServer();
    await expect(
      addDomainsToDraft(clientFor(mock), {
        list: "missing",
        domains: ["example.com"],
        createIfMissing: false,
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/does not exist/);
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(0);
  });

  it("rejects invalid domains and IP addresses before staging", async () => {
    const mock = draftServer();
    await expect(
      addDomainsToDraft(clientFor(mock), {
        list: "existing",
        domains: ["not a domain"],
        createIfMissing: true,
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/not a valid domain/);

    await expect(
      addDomainsToDraft(clientFor(mock), {
        list: "existing",
        domains: ["1.1.1.1"],
        createIfMissing: true,
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/IP address/);
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(0);
  });

  it("saves and waits for the apply when save=true", async () => {
    const mock = draftServer()
      .onJson("POST", "/api/config/save", 202, { operation_id: "op-1", status: "accepted" })
      .onJson("GET", "/api/health/service", 200, {
        ...healthyHealth(),
        lifecycle_operation: {
          id: "op-1",
          type: "apply_config",
          status: "succeeded",
          started_at: 1,
          stages: [],
        },
      });

    const result = await addDomainsToDraft(clientFor(mock), {
      list: "existing",
      domains: ["new.example"],
      createIfMissing: true,
      save: true,
      saveWaitSeconds: 5,
    });

    expect(result.saved).toMatchObject({ accepted: true, operation_id: "op-1", finished: true, status: "succeeded" });
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(1);
    expect(mock.requestsFor("POST", "/api/config/save")).toHaveLength(1);
  });
});

describe("remove_domain_from_list read-modify-write", () => {
  it("removes exact and wildcard matches and stages the remaining domains", async () => {
    const config = baseConfig();
    config.lists = {
      existing: { domains: ["example.com", "*.example.org", "keep.me"] },
    };
    const mock = draftServer(config);

    const result = await removeDomainsFromDraft(clientFor(mock), {
      list: "existing",
      domains: ["EXAMPLE.com.", "*.example.org", "missing.example"],
      save: false,
      saveWaitSeconds: 20,
    });

    expect(result.changed).toBe(true);
    expect(result.change_summary).toEqual({
      list: "existing",
      removed: ["example.com", "*.example.org"],
      not_found: ["missing.example"],
    });
    const body = mock.requestsFor("POST", "/api/config")[0]?.body as {
      lists: Record<string, { domains?: string[] }>;
    };
    expect(body.lists.existing?.domains).toEqual(["keep.me"]);
  });

  it("refuses to empty a list that has no other source", async () => {
    const config = baseConfig();
    config.lists = { only: { domains: ["example.com"] } };
    const mock = draftServer(config);

    await expect(
      removeDomainsFromDraft(clientFor(mock), {
        list: "only",
        domains: ["example.com"],
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/empty and invalid/);
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(0);
  });

  it("rejects unknown lists", async () => {
    const mock = draftServer();
    await expect(
      removeDomainsFromDraft(clientFor(mock), {
        list: "nope",
        domains: ["example.com"],
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/does not exist/);
  });
});

describe("create_list", () => {
  it("validates CIDRs and outbound detours", async () => {
    const mock = draftServer();
    await expect(
      createListInDraft(clientFor(mock), {
        name: "bad_cidr",
        ipCidrs: ["192.168.0.0/33"],
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/Invalid IP addresses or CIDRs/);

    await expect(
      createListInDraft(clientFor(mock), {
        name: "bad_detour",
        domains: ["example.com"],
        detour: "nope",
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/Outbound "nope" does not exist/);
  });

  it("creates an IP list in the draft", async () => {
    const mock = draftServer();
    const result = await createListInDraft(clientFor(mock), {
      name: "my_ips",
      ipCidrs: ["10.0.0.0/8", "1.1.1.1"],
      save: false,
      saveWaitSeconds: 20,
    });
    expect(result.changed).toBe(true);
    const body = mock.requestsFor("POST", "/api/config")[0]?.body as {
      lists: Record<string, { ip_cidrs?: string[] }>;
    };
    expect(body.lists.my_ips?.ip_cidrs).toEqual(["10.0.0.0/8", "1.1.1.1"]);
  });
});

describe("write gate", () => {
  it("explains how to enable mutations", () => {
    expect(() => requireWrite(false)).toThrow(/KEEN_PBR_ALLOW_WRITE=1/);
    expect(() => requireWrite(true)).not.toThrow();
  });
});

describe("environment config", () => {
  it("requires a URL and a token", () => {
    expect(() => loadConfig({})).toThrow(/KEEN_PBR_URL is required/);
    expect(() => loadConfig({ KEEN_PBR_URL: "http://router:12121" })).toThrow(/KEEN_PBR_TOKEN is required/);
    expect(
      loadConfig({ KEEN_PBR_URL: "http://router:12121", KEEN_PBR_TOKEN: "abc" }),
    ).toMatchObject({
      baseUrl: "http://router:12121",
      token: "abc",
      allowWrite: false,
    });
  });

  it("normalizes trailing slashes and only enables writes for exactly 1", () => {
    expect(
      loadConfig({ KEEN_PBR_URL: "http://router:8080/prefix/", KEEN_PBR_TOKEN: "t", KEEN_PBR_ALLOW_WRITE: "1" }),
    ).toMatchObject({ baseUrl: "http://router:8080/prefix", allowWrite: true });
    expect(
      loadConfig({ KEEN_PBR_URL: "http://router:8080", KEEN_PBR_TOKEN: "t", KEEN_PBR_ALLOW_WRITE: "true" }),
    ).toMatchObject({ allowWrite: false });
  });

  it("ignores the removed password variable", () => {
    const config = loadConfig({
      KEEN_PBR_URL: "http://router",
      KEEN_PBR_TOKEN: "t",
      KEEN_PBR_PASSWORD: "secret",
    });
    expect(config).not.toHaveProperty("password");
    expect(JSON.stringify(config)).not.toContain("secret");
  });
});
