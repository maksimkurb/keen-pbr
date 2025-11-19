# ConfigHasher Feature Implementation Plan

## Overview
Implement a configuration hashing system that generates MD5 hashes of the current and applied configurations, enabling the UI to detect when configuration changes require service restart.

## Goals
1. Generate deterministic MD5 hash of configuration state
2. Track both current config (from file) and applied config (in running service)
3. Display configuration mismatch warning in UI
4. Cache current config hash for performance

---

## Phase 1: ConfigHasher Module

### File: `src/internal/config/hasher.go`

**Purpose:** Calculate MD5 hash of configuration including all used lists.

**Key Functions:**

```go
package config

import (
    "crypto/md5"
    "encoding/hex"
    "encoding/json"
    "fmt"
    "io"
    "os"
    "sort"
)

// ConfigHasher calculates MD5 hash of configuration state
type ConfigHasher struct {
    config *Config
}

// NewConfigHasher creates a new config hasher
func NewConfigHasher(cfg *Config) *ConfigHasher {
    return &ConfigHasher{config: cfg}
}

// CalculateHash generates MD5 hash of entire configuration
func (h *ConfigHasher) CalculateHash() (string, error) {
    // 1. Get used list names from all ipsets
    usedLists := h.getUsedListNames()

    // 2. Build hashable structure
    hashData := &ConfigHashData{
        General:  h.config.General,
        IPSets:   h.buildIPSetHashData(),
        ListMD5s: h.calculateListHashes(usedLists),
    }

    // 3. Serialize to JSON (sorted keys for determinism)
    jsonBytes, err := json.Marshal(hashData)
    if err != nil {
        return "", fmt.Errorf("failed to marshal config data: %w", err)
    }

    // 4. Calculate MD5
    hash := md5.Sum(jsonBytes)
    return hex.EncodeToString(hash[:]), nil
}

// getUsedListNames returns set of list names actually used in ipsets
func (h *ConfigHasher) getUsedListNames() map[string]bool {
    used := make(map[string]bool)
    for _, ipset := range h.config.IPSets {
        for _, listName := range ipset.Lists {
            used[listName] = true
        }
    }
    return used
}

// buildIPSetHashData creates hashable representation of ipsets
func (h *ConfigHasher) buildIPSetHashData() []*IPSetHashData {
    result := make([]*IPSetHashData, len(h.config.IPSets))
    for i, ipset := range h.config.IPSets {
        result[i] = &IPSetHashData{
            IPSetName:           ipset.IPSetName,
            Lists:               sortedStrings(ipset.Lists),
            IPVersion:           ipset.IPVersion,
            FlushBeforeApplying: ipset.FlushBeforeApplying,
            Routing:             ipset.Routing,
            IPTablesRules:       ipset.IPTablesRules,
        }
    }
    return result
}

// calculateListHashes generates MD5 for each used list
func (h *ConfigHasher) calculateListHashes(usedLists map[string]bool) map[string]string {
    hashes := make(map[string]string)

    for _, list := range h.config.Lists {
        if !usedLists[list.ListName] {
            continue // Skip unused lists
        }

        hash, err := h.calculateListHash(list)
        if err != nil {
            // Use error as hash to indicate problem
            hashes[list.ListName] = fmt.Sprintf("error:%v", err)
        } else {
            hashes[list.ListName] = hash
        }
    }

    return hashes
}

// calculateListHash calculates hash for a single list
func (h *ConfigHasher) calculateListHash(list *ListSource) (string, error) {
    switch list.Type() {
    case "url":
        return h.hashListFile(list)
    case "file":
        return h.hashListFile(list)
    case "hosts":
        return h.hashInlineHosts(list)
    default:
        return "", fmt.Errorf("unknown list type")
    }
}

// hashListFile calculates MD5 of a file-based list
func (h *ConfigHasher) hashListFile(list *ListSource) (string, error) {
    path, err := list.GetAbsolutePath(h.config)
    if err != nil {
        return "", err
    }

    file, err := os.Open(path)
    if err != nil {
        return "", fmt.Errorf("failed to open list file: %w", err)
    }
    defer file.Close()

    hash := md5.New()
    if _, err := io.Copy(hash, file); err != nil {
        return "", fmt.Errorf("failed to hash list file: %w", err)
    }

    return hex.EncodeToString(hash.Sum(nil)), nil
}

// hashInlineHosts calculates MD5 of inline hosts array
func (h *ConfigHasher) hashInlineHosts(list *ListSource) (string, error) {
    // Sort for deterministic hashing
    sorted := make([]string, len(list.Hosts))
    copy(sorted, list.Hosts)
    sort.Strings(sorted)

    // Serialize and hash
    data, err := json.Marshal(sorted)
    if err != nil {
        return "", err
    }

    hash := md5.Sum(data)
    return hex.EncodeToString(hash[:]), nil
}

// Helper types for hashing

type ConfigHashData struct {
    General  *GeneralConfig                `json:"general"`
    IPSets   []*IPSetHashData              `json:"ipsets"`
    ListMD5s map[string]string             `json:"list_md5s"`
}

type IPSetHashData struct {
    IPSetName           string          `json:"ipset_name"`
    Lists               []string        `json:"lists"`
    IPVersion           IpFamily        `json:"ip_version"`
    FlushBeforeApplying bool            `json:"flush_before_applying"`
    Routing             *RoutingConfig  `json:"routing,omitempty"`
    IPTablesRules       []*IPTablesRule `json:"iptables_rules,omitempty"`
}

func sortedStrings(s []string) []string {
    result := make([]string, len(s))
    copy(result, s)
    sort.Strings(result)
    return result
}
```

**What's Included in Hash:**
- ✅ General settings (ListsOutputDir, UseKeeneticDNS, FallbackDNS, APIBindAddress)
- ✅ All IPSet configurations (name, IP version, flush setting)
- ✅ Routing configs (interfaces, kill-switch, fwmark, table, priority, DNS override)
- ✅ IPTables rules (table, chain, rule parts)
- ✅ MD5 hashes of all lists actually used in ipsets
- ✅ List file contents (url/file) or inline hosts

**What's Excluded:**
- ❌ Lists not referenced by any ipset
- ❌ Internal state (_absConfigFilePath)

---

## Phase 2: Service Manager Integration

### File: `src/internal/service/manager.go` (new file)

**Purpose:** Track applied configuration hash at service startup.

```go
package service

import (
    "github.com/maksimkurb/keen-pbr/src/internal/config"
    "sync"
)

// ServiceManager manages the keen-pbr service lifecycle
type ServiceManager struct {
    appliedConfigHash string
    mu                sync.RWMutex
}

// NewServiceManager creates a new service manager
func NewServiceManager() *ServiceManager {
    return &ServiceManager{}
}

// SetAppliedConfigHash stores the hash of configuration
// that's currently applied and running
func (sm *ServiceManager) SetAppliedConfigHash(hash string) {
    sm.mu.Lock()
    defer sm.mu.Unlock()
    sm.appliedConfigHash = hash
}

// GetAppliedConfigHash returns the hash of applied configuration
func (sm *ServiceManager) GetAppliedConfigHash() string {
    sm.mu.RLock()
    defer sm.mu.RUnlock()
    return sm.appliedConfigHash
}
```

### Integration in Service Startup

**File: `src/internal/commands/start.go` (or wherever service starts)**

```go
// When service starts, calculate and store config hash
func (s *StartCommand) Run() error {
    // ... existing startup code ...

    // Calculate current config hash
    hasher := config.NewConfigHasher(s.cfg)
    configHash, err := hasher.CalculateHash()
    if err != nil {
        log.Warnf("Failed to calculate config hash: %v", err)
        configHash = "unknown"
    }

    // Store in service manager
    s.ctx.ServiceManager.SetAppliedConfigHash(configHash)
    log.Infof("Service started with config hash: %s", configHash)

    // ... rest of startup ...
}
```

---

## Phase 3: API Status Endpoint Enhancement

### File: `src/internal/api/types.go`

**Add field to StatusResponse:**

```go
type StatusResponse struct {
    Version              VersionInfo            `json:"version"`
    KeeneticVersion      string                 `json:"keenetic_version,omitempty"`
    Services             map[string]ServiceInfo `json:"services"`
    CurrentConfigHash    string                 `json:"current_config_hash"`      // NEW
    AppliedConfigHash    string                 `json:"applied_config_hash"`      // NEW
    ConfigurationOutdated bool                  `json:"configuration_outdated"`   // NEW
}

type ServiceInfo struct {
    Status       string `json:"status"`  // "running", "stopped", "unknown"
    Message      string `json:"message,omitempty"`
    ConfigHash   string `json:"config_hash,omitempty"`  // NEW - for keen-pbr service
}
```

### File: `src/internal/api/status.go`

**Add hash caching:**

```go
import (
    "time"
    "sync"
)

// Hash cache with 5-minute TTL
type hashCache struct {
    hash      string
    timestamp time.Time
    mu        sync.RWMutex
}

var currentConfigHashCache = &hashCache{}

const hashCacheTTL = 5 * time.Minute

// getCachedConfigHash returns cached hash or recalculates if expired
func (h *Handler) getCachedConfigHash() (string, error) {
    currentConfigHashCache.mu.RLock()
    if time.Since(currentConfigHashCache.timestamp) < hashCacheTTL &&
       currentConfigHashCache.hash != "" {
        hash := currentConfigHashCache.hash
        currentConfigHashCache.mu.RUnlock()
        return hash, nil
    }
    currentConfigHashCache.mu.RUnlock()

    // Need to recalculate
    currentConfigHashCache.mu.Lock()
    defer currentConfigHashCache.mu.Unlock()

    // Double-check after acquiring write lock
    if time.Since(currentConfigHashCache.timestamp) < hashCacheTTL &&
       currentConfigHashCache.hash != "" {
        return currentConfigHashCache.hash, nil
    }

    // Load current config from file
    cfg, err := config.LoadConfig(h.deps.ConfigPath())
    if err != nil {
        return "", fmt.Errorf("failed to load config: %w", err)
    }

    // Calculate hash
    hasher := config.NewConfigHasher(cfg)
    hash, err := hasher.CalculateHash()
    if err != nil {
        return "", fmt.Errorf("failed to calculate hash: %w", err)
    }

    // Update cache
    currentConfigHashCache.hash = hash
    currentConfigHashCache.timestamp = time.Now()

    return hash, nil
}

// GetStatus returns system status information.
// GET /api/v1/status
func (h *Handler) GetStatus(w http.ResponseWriter, r *http.Request) {
    response := StatusResponse{
        Version: VersionInfo{
            Version: Version,
            Date:    Date,
            Commit:  Commit,
        },
        Services: make(map[string]ServiceInfo),
    }

    // Get Keenetic version if available
    if h.deps.KeeneticClient() != nil {
        if versionStr, err := h.deps.KeeneticClient().GetRawVersion(); err == nil {
            response.KeeneticVersion = versionStr
        }
    }

    // Calculate current config hash (cached)
    currentHash, err := h.getCachedConfigHash()
    if err != nil {
        log.Warnf("Failed to get current config hash: %v", err)
        currentHash = "error"
    }
    response.CurrentConfigHash = currentHash

    // Get applied config hash from service
    appliedHash := h.serviceMgr.GetAppliedConfigHash()
    response.AppliedConfigHash = appliedHash

    // Compare hashes
    response.ConfigurationOutdated = (currentHash != "" &&
                                      appliedHash != "" &&
                                      currentHash != appliedHash &&
                                      currentHash != "error")

    // Check keen-pbr service status
    keenPbrInfo := h.getKeenPbrServiceStatus()
    keenPbrInfo.ConfigHash = appliedHash
    response.Services["keen-pbr"] = keenPbrInfo

    // Check dnsmasq service status
    response.Services["dnsmasq"] = getServiceStatus("dnsmasq", "/opt/etc/init.d/S56dnsmasq")

    writeJSONData(w, response)
}
```

---

## Phase 4: Frontend Integration

### File: `src/frontend/components/dashboard/ServiceStatusWidget.tsx`

**Add config outdated detection:**

```tsx
export function ServiceStatusWidget() {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const [controlLoading, setControlLoading] = useState<string | null>(null);

  const { data, isLoading, error } = useQuery({
    queryKey: ['status'],
    queryFn: () => apiClient.getStatus(),
    refetchInterval: 5000, // Refresh every 5 seconds
  });

  // ... existing code ...

  const keenPbrStatus = data.services['keen-pbr']?.status || 'unknown';
  const dnsmasqStatus = data.services.dnsmasq?.status || 'unknown';
  const configOutdated = data.configuration_outdated || false;

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('dashboard.systemStatus')}</CardTitle>
      </CardHeader>
      <CardContent>
        <div className="space-y-4">
          {/* Configuration outdated warning */}
          {configOutdated && (
            <Alert className="bg-yellow-50 border-yellow-200 dark:bg-yellow-950 dark:border-yellow-800">
              <AlertTriangle className="h-4 w-4 text-yellow-600 dark:text-yellow-400" />
              <AlertDescription className="text-yellow-800 dark:text-yellow-200">
                {t('dashboard.configurationOutdated')}
              </AlertDescription>
            </Alert>
          )}

          <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-4">
            <StatusCard
              title={t('dashboard.version')}
              value={`${data.version.version} (${data.version.commit})`}
            />
            <StatusCard
              title={t('dashboard.keeneticVersion')}
              value={data.keenetic_version || t('common.notAvailable')}
            />
            <StatusCard
              title={t('dashboard.keenPbrService')}
              status={keenPbrStatus}
              // Add yellow background if config outdated
              className={configOutdated ? 'bg-yellow-50 dark:bg-yellow-950' : ''}
              actions={
                <>
                  {/* ... existing buttons ... */}
                </>
              }
            />
            <StatusCard
              title={t('dashboard.dnsmasqService')}
              status={dnsmasqStatus}
              actions={
                <Button
                  size="sm"
                  variant="outline"
                  onClick={() => handleServiceControl('dnsmasq', 'restart')}
                  disabled={controlLoading === 'dnsmasq-restart'}
                >
                  <RotateCw className="h-3 w-3 mr-1" />
                  {t('common.restart')}
                </Button>
              }
            />
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
```

### File: `src/frontend/components/dashboard/StatusCard.tsx`

**Update to accept className prop:**

```tsx
interface StatusCardProps {
  title: string;
  value?: string;
  status?: string;
  actions?: React.ReactNode;
  className?: string;  // NEW
}

export function StatusCard({ title, value, status, actions, className }: StatusCardProps) {
  const { t } = useTranslation();

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'running':
        return 'text-green-600';
      case 'stopped':
        return 'text-red-600';
      default:
        return 'text-gray-500';
    }
  };

  return (
    <div className={`rounded-lg border bg-card p-4 ${className || ''}`}>
      {/* ... rest of component ... */}
    </div>
  );
}
```

### File: `src/frontend/src/api/client.ts`

**Update types:**

```typescript
export interface StatusResponse {
  version: VersionInfo;
  keenetic_version?: string;
  services: Record<string, ServiceInfo>;
  current_config_hash: string;      // NEW
  applied_config_hash: string;      // NEW
  configuration_outdated: boolean;  // NEW
}

export interface ServiceInfo {
  status: 'running' | 'stopped' | 'unknown';
  message?: string;
  config_hash?: string;  // NEW
}
```

### Translation Files

**File: `src/frontend/src/i18n/en.json`**

```json
{
  "dashboard": {
    "systemStatus": "System Status",
    "configurationOutdated": "Configuration has been modified. Restart keen-pbr service to apply changes.",
    ...
  }
}
```

**File: `src/frontend/src/i18n/ru.json`**

```json
{
  "dashboard": {
    "systemStatus": "Статус системы",
    "configurationOutdated": "Конфигурация была изменена. Перезапустите службу keen-pbr для применения изменений.",
    ...
  }
}
```

---

## Phase 5: Testing Plan

### Unit Tests

**File: `src/internal/config/hasher_test.go`**

```go
func TestConfigHasher_DeterministicHash(t *testing.T) {
    // Same config should produce same hash
}

func TestConfigHasher_UnusedListsIgnored(t *testing.T) {
    // Adding unused list shouldn't change hash
}

func TestConfigHasher_UsedListsIncluded(t *testing.T) {
    // Changing used list content should change hash
}

func TestConfigHasher_OrderIndependent(t *testing.T) {
    // Reordering lists in ipset shouldn't change hash (sorted)
}

func TestConfigHasher_AllSettingsIncluded(t *testing.T) {
    // Changing routing/iptables settings should change hash
}
```

### Integration Tests

1. Start service, verify applied hash stored
2. Modify config file, verify status endpoint shows different hash
3. Restart service, verify hashes match again
4. Verify UI shows warning when hashes differ

---

## Implementation Order

1. **Phase 1**: Implement ConfigHasher module with tests
2. **Phase 2**: Add ServiceManager hash tracking
3. **Phase 3**: Update API status endpoint
4. **Phase 4**: Update frontend UI with warning
5. **Phase 5**: Testing and validation

---

## Security Considerations

- MD5 is sufficient for config change detection (not cryptographic security)
- No sensitive data exposed (hashes only)
- Cache prevents DOS via repeated hash calculations
- File access limited to config directory

---

## Performance Considerations

- Hash calculation: ~1-5ms for typical configs
- 5-minute cache prevents excessive recalculation
- File reading optimized with streaming hash
- No impact on hot paths (only status endpoint)

---

## Error Handling

- Failed hash calculation → "error" or "unknown" value
- Missing list files → include error in hash (deterministic)
- Service not started → empty applied hash
- Cache expiration → automatic recalculation

---

## Future Enhancements (Optional)

1. Add hash to service logs for debugging
2. Show last config change timestamp in UI
3. Add "Apply & Restart" button to UI
4. Track hash history for rollback
5. Add hash to health check endpoint

---

## Example Output

### API Response (`/api/v1/status`):

```json
{
  "version": {
    "version": "1.0.0",
    "date": "2025-01-19",
    "commit": "abc123"
  },
  "keenetic_version": "4.1.7",
  "current_config_hash": "5d41402abc4b2a76b9719d911017c592",
  "applied_config_hash": "098f6bcd4621d373cade4e832627b4f6",
  "configuration_outdated": true,
  "services": {
    "keen-pbr": {
      "status": "running",
      "message": "Service is running",
      "config_hash": "098f6bcd4621d373cade4e832627b4f6"
    },
    "dnsmasq": {
      "status": "running",
      "message": "Service is running"
    }
  }
}
```

### UI Display:

```
┌─────────────────────────────────────────────────────────────┐
│ ⚠️  Configuration has been modified. Restart keen-pbr      │
│     service to apply changes.                               │
└─────────────────────────────────────────────────────────────┘

┌──────────────┬──────────────┬──────────────┬──────────────┐
│ Version      │ Keenetic     │ keen-pbr     │ dnsmasq      │
│ 1.0.0        │ 4.1.7        │ 🟨 Running   │ 🟢 Running   │
│ (abc123)     │              │ [Start/Stop/ │ [Restart]    │
│              │              │  Restart]    │              │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

---

## Acceptance Criteria

- ✅ Config hash calculated deterministically
- ✅ Only used lists included in hash
- ✅ All routing/iptables/general settings included
- ✅ Hash cached for 5 minutes
- ✅ Applied hash stored at service startup
- ✅ Status endpoint returns both hashes
- ✅ UI shows yellow warning when hashes differ
- ✅ Warning text is i18n translated
- ✅ Tests cover all hash scenarios
- ✅ Performance impact < 10ms per status check
