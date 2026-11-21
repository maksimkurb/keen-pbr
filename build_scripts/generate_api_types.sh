#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$REPO_ROOT/src/api/generated"

# Step 1: Extract components/schemas from the OpenAPI 3.1 YAML and create a
# JSON Schema $defs document with a synthetic root that references ALL schemas.
# This ensures QuickType generates types for config, health, cache, and routing.
# Uses only Node.js tooling: npx js-yaml + node built-ins.
SCHEMA_TMP="$(mktemp /tmp/keen-pbr-schema-XXXXXX.json)"
TYPES_TMP="$(mktemp /tmp/keen-pbr-types-XXXXXX.hpp)"
trap 'rm -f "$SCHEMA_TMP" "$TYPES_TMP"' EXIT

npx --yes js-yaml "$REPO_ROOT/docs/openapi.yaml" \
  | node -e "
let d = '';
process.stdin.on('data', c => d += c);
process.stdin.on('end', () => {
  const spec = JSON.parse(d);
  const schemas = spec.components.schemas;

  // Inline enums and objects have no schema name, so QuickType would name
  // them after the property alone. Require them to be named schemas.
  const inline = [];
  function findInline(node, path, top) {
    if (typeof node !== 'object' || node === null) return;
    if (node['\$ref']) return;
    if (!top && (node.enum || node.properties)) inline.push(path);
    for (const [k, v] of Object.entries(node.properties || {})) findInline(v, path + '.' + k, false);
    if (node.items) findInline(node.items, path + '[]', false);
    if (node.additionalProperties) findInline(node.additionalProperties, path + '{}', false);
    for (const k of ['oneOf', 'anyOf', 'allOf']) {
      (node[k] || []).forEach((s, i) => findInline(s, path + '/' + k + '[' + i + ']', false));
    }
  }
  for (const [name, schema] of Object.entries(schemas)) findInline(schema, name, true);
  if (inline.length > 0) {
    console.error('generate_api_types: inline enums/objects must be moved to components/schemas ' +
                  'and referenced with \$ref: ' + inline.join(', '));
    process.exit(1);
  }

  // password_hash is daemon-internal configuration state. Keep it out of the
  // public OpenAPI contract (and therefore out of browser-generated models),
  // while retaining it in the C++ configuration representation.
  schemas.AuthenticationConfig.properties.password_hash = { type: 'string' };

  // API tokens are stored in the config as hashes. The record (with its hash)
  // is likewise daemon-internal; the REST contract exposes ApiTokenInfo.
  schemas.ApiTokenConfig = {
    type: 'object',
    properties: {
      id: { type: 'string' },
      name: { type: 'string' },
      scopes: { type: 'array', items: { \$ref: '#/components/schemas/ApiTokenScope' } },
      token_hash: { type: 'string' },
      created_at: { type: 'integer' },
      expires_at: { type: 'integer' }
    }
  };
  schemas.AuthenticationConfig.properties.tokens = {
    type: 'array',
    items: { \$ref: '#/components/schemas/ApiTokenConfig' }
  };

  // Remove OpenAPI-specific extensions that QuickType does not understand
  function clean(obj) {
    if (typeof obj !== 'object' || obj === null) return;
    if (Array.isArray(obj)) { obj.forEach(clean); return; }
    delete obj.discriminator;
    delete obj.example;
    delete obj.description;
    for (const [key, value] of Object.entries(obj)) {
      // A schema properties member is a map of property names. Do not mistake
      // a property literally named description for OpenAPI metadata.
      if (key === 'properties' && value && typeof value === 'object') {
        Object.values(value).forEach(clean);
      } else {
        clean(value);
      }
    }
  }
  clean(schemas);

  // QuickType names a type after the property it is first reached through
  // (OutboundElement, Bypass, Code, ...) unless the schema carries a title.
  // Pin every component schema to its OpenAPI name so C++ names match it.
  for (const [name, schema] of Object.entries(schemas)) {
    schema.title = name;
  }

  // Rewrite \$ref paths from OpenAPI to JSON Schema \$defs format
  function rewriteRefs(obj) {
    if (typeof obj !== 'object' || obj === null) return;
    if (Array.isArray(obj)) { obj.forEach(rewriteRefs); return; }
    if (obj['\$ref']) {
      obj['\$ref'] = obj['\$ref'].replace('#/components/schemas/', '#/\$defs/');
    }
    Object.values(obj).forEach(rewriteRefs);
  }
  rewriteRefs(schemas);

  // Synthetic root: reference ALL schemas so QuickType generates every type.
  // Each schema becomes a property of the root, driving code generation.
  const rootProperties = {};
  for (const name of Object.keys(schemas)) {
    rootProperties[name] = { '\$ref': '#/\$defs/' + name };
  }

  const jsonSchema = {
    '\$schema': 'http://json-schema.org/draft-07/schema#',
    '\$defs': schemas,
    'type': 'object',
    'properties': rootProperties
  };

  process.stdout.write(JSON.stringify(jsonSchema, null, 2));
});
" > "$SCHEMA_TMP"

# Step 2: Run QuickType to generate C++ types
npx --yes quicktype \
  --lang cpp \
  --src "$SCHEMA_TMP" \
  --src-lang schema \
  --namespace keen_pbr3::api \
  --top-level KeenPbrTypes \
  --no-boost \
  --code-format with-struct \
  -o "$TYPES_TMP"

# Step 3: Post-process: fix include path and prepend required system headers
node -e "
const fs = require('fs');
let content = fs.readFileSync('$TYPES_TMP', 'utf8');

// Fix nlohmann include path
content = content.replace('#include \"json.hpp\"', '#include <nlohmann/json.hpp>');

// Add cstdint and map includes (QuickType omits them)
content = content.replace(
  '#include <optional>',
  '#include <cstdint>\n#include <map>\n#include <optional>'
);

// RuntimeInterfaceState.interface_name is optional because table candidates do
// not have an interface. QuickType writes empty optionals as JSON null; preserve
// the OpenAPI omission semantics for this field in REST and status-stream data.
const quote = String.fromCharCode(34);
content = content.replace(
  'j[' + quote + 'detail' + quote + '] = x.detail;\n' +
    '        j[' + quote + 'interface_name' + quote + '] = x.interface_name;',
  'j[' + quote + 'detail' + quote + '] = x.detail;\n' +
    '        if (x.interface_name.has_value()) j[' + quote + 'interface_name' + quote + '] = *x.interface_name;'
);

// Every OpenAPI schema must exist under its own name in the header, so the C++
// type names stay equal to the schema names.
const schemaNames = JSON.parse(fs.readFileSync('$SCHEMA_TMP', 'utf8'))['\$defs'];
const missing = Object.keys(schemaNames).filter(name =>
  !new RegExp('^ *(struct|enum class) ' + name + '[^A-Za-z0-9_]', 'm').test(content));
if (missing.length > 0) {
  console.error('generate_api_types: no generated type named after these OpenAPI schemas: ' +
                missing.join(', '));
  process.exit(1);
}

// Add generation comment at the top
const header = '// Generated from docs/openapi.yaml via build_scripts/generate_api_types.sh\n' +
               '// Run \"make generate\" to regenerate (requires Node.js).\n\n';
content = header + content;

fs.writeFileSync('$REPO_ROOT/src/api/generated/api_types.hpp', content);
"

# Step 4: Generate the route table (who may call which operation) from the
# `security` of every operation in docs/openapi.yaml. The daemon enforces API
# token scopes from this table, so the document is the single source of truth.
npx --yes js-yaml "$REPO_ROOT/docs/openapi.yaml" \
  | node -e "
const fs = require('fs');
let d = '';
process.stdin.on('data', c => d += c);
process.stdin.on('end', () => {
  const spec = JSON.parse(d);
  const scopes = { 'read:prometheus': 'READ_PROMETHEUS', 'read': 'READ',
                   'write:rules': 'WRITE_RULES', 'write': 'WRITE' };
  const rows = [];
  for (const [path, item] of Object.entries(spec.paths)) {
    for (const method of ['get', 'post', 'put', 'patch', 'delete']) {
      const op = item[method];
      if (!op) continue;
      const security = op.security !== undefined ? op.security : spec.security;
      let access = 'SessionOnly';
      let scope = 'WRITE';
      if (!security || security.length === 0) {
        access = 'Public';
      } else {
        const token = security.find(s => s.apiToken !== undefined);
        if (token) {
          const names = token.apiToken;
          if (names.length !== 1 || !(names[0] in scopes)) {
            console.error('generate_api_types: ' + method + ' ' + path +
                          ' needs exactly one known apiToken scope');
            process.exit(1);
          }
          access = 'Scope';
          scope = scopes[names[0]];
        }
      }
      rows.push({ method: method.toUpperCase(), path, access, scope });
    }
  }
  let out = '// Generated from docs/openapi.yaml via build_scripts/generate_api_types.sh\n' +
            '// Run \"make generate\" to regenerate (requires Node.js).\n\n' +
            '#pragma once\n\n#include \"api_types.hpp\"\n\n#include <array>\n#include <string_view>\n\n' +
            'namespace keen_pbr3::api {\n\n' +
            '// Public: no authentication. SessionOnly: the administrator password or a WebUI\n' +
            '// session, never an API token. Scope: an API token holding \`scope\` (or wider).\n' +
            'enum class RouteAccess { Public, SessionOnly, Scope };\n\n' +
            'struct ApiRoute {\n    std::string_view method;\n    std::string_view path;\n' +
            '    RouteAccess access;\n    ApiTokenScope scope;\n};\n\n' +
            'inline constexpr std::array<ApiRoute, ' + rows.length + '> kApiRoutes{{\n';
  for (const r of rows) {
    out += '    {\"' + r.method + '\", \"' + r.path + '\", RouteAccess::' + r.access +
           ', ApiTokenScope::' + r.scope + '},\n';
  }
  out += '}};\n\n} // namespace keen_pbr3::api\n';
  fs.writeFileSync('$REPO_ROOT/src/api/generated/api_routes.hpp', out);
});
"
