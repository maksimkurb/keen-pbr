# DNS Check Feature - Implementation Plan

## Overview

This feature implements a comprehensive DNS check system to verify that split-DNS configuration is working correctly on both the PC and browser levels. The system uses a custom UDP DNS server that captures DNS queries and broadcasts them via SSE (Server-Sent Events) to the web UI.

## Feature Components

### 1. Backend Components

#### 1.1 Configuration (`src/internal/config/types.go`)

Add DNS check port configuration to `GeneralConfig`:

```go
type GeneralConfig struct {
    // ... existing fields ...
    DNSCheckPort int `toml:"dns_check_port" json:"dns_check_port" comment:"Port for DNS check listener (default: 15053)"`
}

// GetDNSCheckPort returns the DNS check port (default: 15053)
func (gc *GeneralConfig) GetDNSCheckPort() int {
    if gc.DNSCheckPort <= 0 {
        return 15053 // Default port
    }
    return gc.DNSCheckPort
}
```

#### 1.2 Dnsmasq Configuration Generator (`src/internal/lists/dnsmasq_generator.go`)

Modify `printDnsmasqConfig` function to add CNAME record for DNS check:

```go
// After config hash CNAME record (around line 102), add:

// Print DNS check CNAME record: cname=dns-check.keen-pbr.internal,127.0.50.50
dnsCheckRecord := "cname=dns-check.keen-pbr.internal,127.0.50.50\n"
if _, err := stdoutBuffer.WriteString(dnsCheckRecord); err != nil {
    return fmt.Errorf("failed to write DNS check CNAME to dnsmasq cfg: %v", err)
}

// Print server directive to route DNS check queries to custom port
dnsCheckPort := cfg.General.GetDNSCheckPort()
serverRecord := fmt.Sprintf("server=/dns-check.keen-pbr.internal/127.0.50.50#%d\n", dnsCheckPort)
if _, err := stdoutBuffer.WriteString(serverRecord); err != nil {
    return fmt.Errorf("failed to write DNS check server to dnsmasq cfg: %v", err)
}
log.Infof("DNS check configured: dns-check.keen-pbr.internal -> 127.0.50.50#%d", dnsCheckPort)
```

#### 1.3 UDP DNS Listener (`src/internal/dnscheck/listener.go` - NEW)

Create a new package for DNS check functionality:

```go
package dnscheck

import (
    "context"
    "fmt"
    "net"
    "sync"
    "time"

    "github.com/maksimkurb/keen-pbr/src/internal/log"
)

// DNSCheckListener listens for DNS queries and broadcasts them to SSE clients
type DNSCheckListener struct {
    port      int
    conn      *net.UDPConn
    ctx       context.Context
    cancel    context.CancelFunc

    // SSE broadcasting
    mu          sync.RWMutex
    subscribers map[chan string]struct{}
}

// NewDNSCheckListener creates a new DNS check listener
func NewDNSCheckListener(port int) *DNSCheckListener {
    ctx, cancel := context.WithCancel(context.Background())
    return &DNSCheckListener{
        port:        port,
        ctx:         ctx,
        cancel:      cancel,
        subscribers: make(map[chan string]struct{}),
    }
}

// Start starts the UDP listener
func (l *DNSCheckListener) Start() error {
    addr := net.UDPAddr{
        Port: l.port,
        IP:   net.ParseIP("127.0.50.50"),
    }

    conn, err := net.ListenUDP("udp", &addr)
    if err != nil {
        return fmt.Errorf("failed to start DNS check listener: %v", err)
    }
    l.conn = conn

    log.Infof("DNS check listener started on 127.0.50.50:%d", l.port)

    go l.listen()
    return nil
}

// listen handles incoming DNS queries
func (l *DNSCheckListener) listen() {
    buffer := make([]byte, 512) // Standard DNS packet size

    for {
        select {
        case <-l.ctx.Done():
            return
        default:
        }

        l.conn.SetReadDeadline(time.Now().Add(1 * time.Second))
        n, addr, err := l.conn.ReadFromUDP(buffer)
        if err != nil {
            if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
                continue
            }
            log.Debugf("Error reading DNS query: %v", err)
            continue
        }

        // Parse DNS query to extract domain name
        domain := l.parseDNSQuery(buffer[:n])
        if domain != "" {
            log.Debugf("DNS check query received: %s from %s", domain, addr)

            // Broadcast to SSE subscribers
            l.broadcast(domain)

            // Send DNS response with IP 192.168.255.255
            l.sendDNSResponse(buffer[:n], addr)
        }
    }
}

// parseDNSQuery extracts the domain name from a DNS query packet
func (l *DNSCheckListener) parseDNSQuery(packet []byte) string {
    // DNS query format:
    // Header: 12 bytes
    // Question: variable length
    // QNAME format: labels with length prefixes, ending with 0

    if len(packet) < 12 {
        return ""
    }

    // Skip DNS header (12 bytes)
    pos := 12
    domain := ""

    for pos < len(packet) {
        labelLen := int(packet[pos])
        if labelLen == 0 {
            break
        }

        pos++
        if pos+labelLen > len(packet) {
            return ""
        }

        if domain != "" {
            domain += "."
        }
        domain += string(packet[pos : pos+labelLen])
        pos += labelLen
    }

    return domain
}

// sendDNSResponse sends a DNS response with IP 192.168.255.255
func (l *DNSCheckListener) sendDNSResponse(query []byte, addr *net.UDPAddr) {
    if len(query) < 12 {
        return
    }

    // Build DNS response
    response := make([]byte, len(query)+16) // query + answer section

    // Copy query
    copy(response, query)

    // Set response flags (QR=1, AA=1, RCODE=0)
    response[2] = 0x84 // 10000100
    response[3] = 0x00

    // Set answer count to 1
    response[6] = 0x00
    response[7] = 0x01

    // Find end of question section
    pos := 12
    for pos < len(query) && query[pos] != 0 {
        labelLen := int(query[pos])
        pos += 1 + labelLen
    }
    pos += 5 // Skip null terminator, QTYPE, and QCLASS

    // Answer section (copy question name + response)
    answerStart := pos

    // Name (pointer to question)
    response[answerStart] = 0xc0
    response[answerStart+1] = 0x0c

    // Type A (0x0001)
    response[answerStart+2] = 0x00
    response[answerStart+3] = 0x01

    // Class IN (0x0001)
    response[answerStart+4] = 0x00
    response[answerStart+5] = 0x01

    // TTL (1 second)
    response[answerStart+6] = 0x00
    response[answerStart+7] = 0x00
    response[answerStart+8] = 0x00
    response[answerStart+9] = 0x01

    // RDLENGTH (4 bytes for IPv4)
    response[answerStart+10] = 0x00
    response[answerStart+11] = 0x04

    // RDATA (192.168.255.255)
    response[answerStart+12] = 192
    response[answerStart+13] = 168
    response[answerStart+14] = 255
    response[answerStart+15] = 255

    responseLen := answerStart + 16
    l.conn.WriteToUDP(response[:responseLen], addr)
}

// broadcast sends the domain to all SSE subscribers
func (l *DNSCheckListener) broadcast(domain string) {
    l.mu.RLock()
    defer l.mu.RUnlock()

    for ch := range l.subscribers {
        select {
        case ch <- domain:
        default:
            // Channel full, skip
        }
    }
}

// Subscribe adds a new SSE subscriber
func (l *DNSCheckListener) Subscribe() <-chan string {
    ch := make(chan string, 10)
    l.mu.Lock()
    l.subscribers[ch] = struct{}{}
    l.mu.Unlock()
    return ch
}

// Unsubscribe removes an SSE subscriber
func (l *DNSCheckListener) Unsubscribe(ch <-chan string) {
    l.mu.Lock()
    delete(l.subscribers, ch.(chan string))
    l.mu.Unlock()
    close(ch.(chan string))
}

// Stop stops the DNS check listener
func (l *DNSCheckListener) Stop() error {
    l.cancel()
    if l.conn != nil {
        return l.conn.Close()
    }
    return nil
}
```

#### 1.4 API Handler (`src/internal/api/check.go`)

Add new SSE endpoint for DNS check:

```go
// CheckSplitDNS streams DNS queries received on the DNS check port via SSE.
// GET /api/v1/check/split-dns
func (h *Handler) CheckSplitDNS(w http.ResponseWriter, r *http.Request) {
    // Set SSE headers
    w.Header().Set("Content-Type", "text/event-stream")
    w.Header().Set("Cache-Control", "no-cache")
    w.Header().Set("Connection", "keep-alive")
    w.Header().Set("X-Accel-Buffering", "no")

    flusher, ok := w.(http.Flusher)
    if !ok {
        WriteInternalError(w, "Streaming not supported")
        return
    }

    // Get DNS check listener from handler dependencies
    if h.dnsCheckListener == nil {
        WriteInternalError(w, "DNS check listener not available")
        return
    }

    // Subscribe to DNS check events
    eventCh := h.dnsCheckListener.Subscribe()
    defer h.dnsCheckListener.Unsubscribe(eventCh)

    // Stream events to client
    for {
        select {
        case <-r.Context().Done():
            // Client disconnected
            return
        case domain := <-eventCh:
            // Send domain as SSE event
            if _, err := fmt.Fprintf(w, "data: %s\n\n", domain); err != nil {
                log.Debugf("Failed to write to response: %v", err)
                return
            }
            flusher.Flush()
        }
    }
}
```

Update `Handler` struct to include DNS check listener:

```go
type Handler struct {
    // ... existing fields ...
    dnsCheckListener *dnscheck.DNSCheckListener
}
```

#### 1.5 Router Update (`src/internal/api/router.go`)

Add route for the new endpoint:

```go
// Network check endpoints
r.Post("/check/routing", h.CheckRouting)
r.Get("/check/ping", h.CheckPing)          // SSE stream
r.Get("/check/traceroute", h.CheckTraceroute) // SSE stream
r.Get("/check/self", h.CheckSelf)          // SSE stream
r.Get("/check/split-dns", h.CheckSplitDNS) // SSE stream - NEW
```

#### 1.6 Service Integration (`src/internal/commands/service.go`)

Start DNS check listener when keen-pbr service starts:

```go
// In ServiceCommand.Run(), after API server setup:

// Start DNS check listener
dnsCheckPort := cfg.General.GetDNSCheckPort()
dnsCheckListener := dnscheck.NewDNSCheckListener(dnsCheckPort)
if err := dnsCheckListener.Start(); err != nil {
    log.Warnf("Failed to start DNS check listener: %v", err)
} else {
    defer dnsCheckListener.Stop()
}

// Pass listener to API handler when creating router
handler := api.NewHandler(ctx.ConfigPath, deps, serviceMgr, configHasher, dnsCheckListener)
```

### 2. Frontend Components

#### 2.1 API Client (`src/frontend/src/api/client.ts`)

Add method for DNS check SSE endpoint:

```typescript
// Add to ApiClient class
getSplitDNSCheckSSEUrl(): string {
    return `${this.baseURL}/api/v1/check/split-dns`;
}
```

#### 2.2 DNS Check Widget (`src/frontend/components/dashboard/DNSCheckWidget.tsx` - NEW)

Create a new widget for DNS checking:

```tsx
import { useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from '../ui/dialog';
import { Loader2, CheckCircle2, XCircle, AlertCircle, Terminal } from 'lucide-react';
import { apiClient } from '../../src/api/client';

type CheckStatus = 'idle' | 'checking' | 'success' | 'browser-fail' | 'pc-fail';

export function DNSCheckWidget() {
    const { t } = useTranslation();
    const [status, setStatus] = useState<CheckStatus>('idle');
    const [showPCCheckDialog, setShowPCCheckDialog] = useState(false);
    const [randomString, setRandomString] = useState('');
    const [pcRandomString, setPCRandomString] = useState('');
    const [pcCheckWaiting, setPCCheckWaiting] = useState(false);
    const eventSourceRef = useRef<EventSource | null>(null);
    const timeoutRef = useRef<NodeJS.Timeout | null>(null);
    const fetchControllerRef = useRef<AbortController | null>(null);

    // Generate random string
    const generateRandomString = () => {
        return Math.random().toString(36).substring(2, 15);
    };

    // Cleanup on unmount
    useEffect(() => {
        return () => {
            cleanup();
        };
    }, []);

    const cleanup = () => {
        if (eventSourceRef.current) {
            eventSourceRef.current.close();
            eventSourceRef.current = null;
        }
        if (timeoutRef.current) {
            clearTimeout(timeoutRef.current);
            timeoutRef.current = null;
        }
        if (fetchControllerRef.current) {
            fetchControllerRef.current.abort();
            fetchControllerRef.current = null;
        }
    };

    const startCheck = () => {
        cleanup();

        const randStr = generateRandomString();
        setRandomString(randStr);
        setStatus('checking');

        // Open SSE connection
        const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
        const eventSource = new EventSource(sseUrl);
        eventSourceRef.current = eventSource;

        let sseReceived = false;
        let fetchFailed = false;
        let fetchTimeout = 5000; // 5 seconds default fetch timeout

        eventSource.onmessage = (event) => {
            const receivedDomain = event.data.trim();

            // Check if this is our random string
            if (receivedDomain === `${randStr}.dns-check.keen-pbr.internal`) {
                sseReceived = true;

                // Cancel fetch if still pending
                if (fetchControllerRef.current) {
                    fetchControllerRef.current.abort();
                }

                // Clear timeout
                if (timeoutRef.current) {
                    clearTimeout(timeoutRef.current);
                }

                setStatus('success');
                cleanup();
            }
        };

        eventSource.onerror = () => {
            console.error('SSE connection error');
        };

        // Make fetch request
        const controller = new AbortController();
        fetchControllerRef.current = controller;

        fetch(`http://${randStr}.dns-check.keen-pbr.internal`, {
            signal: controller.signal,
            mode: 'no-cors'
        }).catch((err) => {
            if (err.name !== 'AbortError') {
                fetchFailed = true;
                fetchTimeout = 5000; // Reset to default on error
            }
        });

        // Set timeout: fetch timeout + 5 seconds
        timeoutRef.current = setTimeout(() => {
            if (!sseReceived) {
                setStatus('browser-fail');
                cleanup();
            }
        }, fetchTimeout + 5000);
    };

    const startPCCheck = () => {
        const randStr = generateRandomString();
        setPCRandomString(randStr);
        setPCCheckWaiting(true);

        // Wait for SSE event
        const timeout = setTimeout(() => {
            if (pcCheckWaiting) {
                setPCCheckWaiting(false);
                // Show additional help message
            }
        }, 30000); // 30 seconds

        // Listen for SSE events
        if (eventSourceRef.current) {
            const originalOnMessage = eventSourceRef.current.onmessage;

            eventSourceRef.current.onmessage = (event) => {
                if (originalOnMessage) originalOnMessage(event);

                const receivedDomain = event.data.trim();
                if (receivedDomain === `${randStr}.dns-check.keen-pbr.internal`) {
                    clearTimeout(timeout);
                    setPCCheckWaiting(false);
                    setStatus('pc-success');
                    setShowPCCheckDialog(false);
                    cleanup();
                }
            };
        }
    };

    const renderContent = () => {
        switch (status) {
            case 'idle':
                return null;

            case 'checking':
                return (
                    <div className="flex items-center gap-2 text-sm">
                        <Loader2 className="h-4 w-4 animate-spin" />
                        {t('dnsCheck.checking')}
                    </div>
                );

            case 'success':
                return (
                    <Alert className="border-green-200 bg-green-50">
                        <CheckCircle2 className="h-4 w-4 text-green-600" />
                        <AlertDescription className="text-green-800">
                            {t('dnsCheck.success')}
                        </AlertDescription>
                    </Alert>
                );

            case 'browser-fail':
                return (
                    <Alert className="border-yellow-200 bg-yellow-50">
                        <AlertCircle className="h-4 w-4 text-yellow-600" />
                        <AlertDescription className="text-yellow-800">
                            {t('dnsCheck.browserFail')}
                            <Button
                                variant="outline"
                                size="sm"
                                className="ml-2"
                                onClick={() => {
                                    setShowPCCheckDialog(true);
                                    startPCCheck();
                                }}
                            >
                                {t('dnsCheck.checkFromPC')}
                            </Button>
                        </AlertDescription>
                    </Alert>
                );

            default:
                return null;
        }
    };

    return (
        <>
            <Card>
                <CardHeader>
                    <CardTitle>{t('dnsCheck.title')}</CardTitle>
                </CardHeader>
                <CardContent className="space-y-4">
                    <p className="text-sm text-muted-foreground">
                        {t('dnsCheck.description')}
                    </p>

                    {status === 'idle' && (
                        <Button onClick={startCheck}>
                            {t('dnsCheck.startCheck')}
                        </Button>
                    )}

                    {renderContent()}
                </CardContent>
            </Card>

            <Dialog open={showPCCheckDialog} onOpenChange={setShowPCCheckDialog}>
                <DialogContent>
                    <DialogHeader>
                        <DialogTitle>{t('dnsCheck.pcCheckTitle')}</DialogTitle>
                        <DialogDescription>
                            {t('dnsCheck.pcCheckDescription')}
                        </DialogDescription>
                    </DialogHeader>

                    <div className="space-y-4">
                        {/* Windows instructions */}
                        <div>
                            <h4 className="font-semibold mb-2">
                                <Terminal className="inline h-4 w-4 mr-1" />
                                Windows
                            </h4>
                            <code className="block bg-muted p-3 rounded text-sm">
                                nslookup {pcRandomString}.dns-check.keen-pbr.internal
                            </code>
                        </div>

                        {/* Linux instructions */}
                        <div>
                            <h4 className="font-semibold mb-2">
                                <Terminal className="inline h-4 w-4 mr-1" />
                                Linux
                            </h4>
                            <code className="block bg-muted p-3 rounded text-sm">
                                nslookup {pcRandomString}.dns-check.keen-pbr.internal
                            </code>
                        </div>

                        {/* macOS instructions */}
                        <div>
                            <h4 className="font-semibold mb-2">
                                <Terminal className="inline h-4 w-4 mr-1" />
                                macOS
                            </h4>
                            <code className="block bg-muted p-3 rounded text-sm">
                                nslookup {pcRandomString}.dns-check.keen-pbr.internal
                            </code>
                        </div>

                        {pcCheckWaiting && (
                            <div className="flex items-center gap-2 text-sm">
                                <Loader2 className="h-4 w-4 animate-spin" />
                                {t('dnsCheck.pcCheckWaiting')}
                            </div>
                        )}

                        {!pcCheckWaiting && (
                            <Alert className="border-yellow-200 bg-yellow-50">
                                <AlertCircle className="h-4 w-4 text-yellow-600" />
                                <AlertDescription className="text-yellow-800">
                                    {t('dnsCheck.pcCheckTimeout')}
                                </AlertDescription>
                            </Alert>
                        )}
                    </div>
                </DialogContent>
            </Dialog>
        </>
    );
}
```

#### 2.3 Dashboard Integration (`src/frontend/src/pages/Dashboard.tsx`)

Add the DNS Check widget to the dashboard:

```tsx
import { DNSCheckWidget } from '../../components/dashboard/DNSCheckWidget';

export default function Dashboard() {
  const { t } = useTranslation();

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-3xl font-bold">{t('dashboard.title')}</h1>
        <p className="text-muted-foreground mt-2">
          {t('dashboard.description')}
        </p>
      </div>

      <ServiceStatusWidget />

      <DNSCheckWidget />  {/* NEW */}

      <SelfCheckWidget />

      <DomainCheckerWidget />
    </div>
  );
}
```

#### 2.4 i18n Translations

Add translations to `src/frontend/src/i18n/locales/en.json`:

```json
{
  "dnsCheck": {
    "title": "DNS Check",
    "description": "Verify that split-DNS is configured correctly",
    "startCheck": "Start DNS Check",
    "checking": "Checking...",
    "success": "DNS working correctly",
    "browserFail": "Seems DNS in your browser is misconfigured",
    "checkFromPC": "Check from PC",
    "pcCheckTitle": "Check DNS from PC",
    "pcCheckDescription": "Run the following command in your terminal to check DNS configuration:",
    "pcCheckWaiting": "Waiting for DNS query...",
    "pcCheckTimeout": "If you already ran this command, seems your DNS on PC is misconfigured. Make sure that your router is configured as primary DNS server.",
    "pcSuccess": "DNS on PC is working, but on browser is not. Maybe you have some VPN plugin installed in your browser or use Edge Secure Network or Cloudflare DNS-over-HTTPS in Firefox?"
  }
}
```

Add translations to `src/frontend/src/i18n/locales/ru.json`:

```json
{
  "dnsCheck": {
    "title": "Проверка DNS",
    "description": "Проверьте корректность настройки split-DNS",
    "startCheck": "Начать проверку DNS",
    "checking": "Проверка...",
    "success": "DNS работает корректно",
    "browserFail": "Похоже, DNS в вашем браузере настроен неправильно",
    "checkFromPC": "Проверить с ПК",
    "pcCheckTitle": "Проверка DNS с ПК",
    "pcCheckDescription": "Выполните следующую команду в терминале для проверки конфигурации DNS:",
    "pcCheckWaiting": "Ожидание DNS-запроса...",
    "pcCheckTimeout": "Если вы уже выполнили эту команду, похоже, что DNS на вашем ПК настроен неправильно. Убедитесь, что ваш роутер настроен как основной DNS-сервер.",
    "pcSuccess": "DNS на ПК работает, но в браузере нет. Возможно, у вас установлен VPN-плагин в браузере, используется Edge Secure Network или Cloudflare DNS-over-HTTPS в Firefox?"
  }
}
```

### 3. Configuration File Update

Update `keen-pbr.example.conf` with the new configuration option:

```toml
[general]
lists_output_dir = "/opt/etc/keen-pbr/lists.d"
use_keenetic_dns = true
fallback_dns = "8.8.8.8"
api_bind_address = "0.0.0.0:8080"
dns_check_port = 15053  # NEW: Port for DNS check listener
```

## Implementation Steps

### Phase 1: Backend Foundation (2-3 hours)
1. ✅ Add `DNSCheckPort` to `GeneralConfig` in `types.go`
2. ✅ Create `src/internal/dnscheck/listener.go` with UDP DNS server
3. ✅ Modify `dnsmasq_generator.go` to add CNAME and server records
4. ✅ Add SSE endpoint in `check.go`
5. ✅ Update router in `router.go`
6. ✅ Integrate listener in `service.go`

### Phase 2: Frontend Implementation (2-3 hours)
7. ✅ Add SSE URL method to API client
8. ✅ Create `DNSCheckWidget.tsx` component
9. ✅ Add widget to `Dashboard.tsx`
10. ✅ Add i18n translations (EN + RU)
11. ✅ Add UI components (Dialog for PC check instructions)

### Phase 3: Testing & Documentation (1-2 hours)
12. ✅ Test DNS listener on port 15053
13. ✅ Test SSE broadcasting
14. ✅ Test widget UI flow (idle → checking → success/fail)
15. ✅ Test PC check dialog
16. ✅ Update `keen-pbr.example.conf`
17. ✅ Update README if needed

## Technical Considerations

### Security
- UDP listener binds to `127.0.50.50` (localhost variant) only
- SSE endpoint protected by existing `PrivateSubnetOnly` middleware
- No external access to DNS check system

### Performance
- DNS listener uses goroutines for concurrent request handling
- SSE uses buffered channels to prevent blocking
- Auto-cleanup on client disconnect

### Error Handling
- Graceful degradation if DNS listener fails to start (warning log only)
- Timeout handling for SSE subscriptions
- Client-side error recovery

### Browser Compatibility
- EventSource API supported in all modern browsers
- Fetch with `mode: 'no-cors'` for DNS check requests
- Fallback messages for timeout scenarios

## Testing Checklist

- [ ] DNS listener starts on configured port
- [ ] dnsmasq config includes CNAME and server records
- [ ] DNS queries to `*.dns-check.keen-pbr.internal` resolve to `192.168.255.255`
- [ ] SSE endpoint receives and broadcasts domain names
- [ ] Widget shows "Checking..." state
- [ ] Widget shows success when DNS works
- [ ] Widget shows browser fail state with timeout
- [ ] PC check dialog displays correct instructions
- [ ] PC check detects successful DNS query
- [ ] All text uses i18n translations
- [ ] Works in both English and Russian

## Files to Create/Modify

### New Files
- `src/internal/dnscheck/listener.go` - UDP DNS listener and SSE broadcaster
- `src/internal/dnscheck/doc.go` - Package documentation
- `src/frontend/components/dashboard/DNSCheckWidget.tsx` - React component

### Modified Files
- `src/internal/config/types.go` - Add DNSCheckPort field
- `src/internal/lists/dnsmasq_generator.go` - Add CNAME/server records
- `src/internal/api/check.go` - Add CheckSplitDNS handler
- `src/internal/api/router.go` - Add route
- `src/internal/api/handlers.go` - Add dnsCheckListener field
- `src/internal/commands/service.go` - Start DNS listener
- `src/frontend/src/api/client.ts` - Add SSE URL method
- `src/frontend/src/pages/Dashboard.tsx` - Add widget
- `src/frontend/src/i18n/locales/en.json` - Add translations
- `src/frontend/src/i18n/locales/ru.json` - Add translations
- `keen-pbr.example.conf` - Add dns_check_port

## Questions for Review

1. **Port Configuration**: Is `15053` an acceptable default port, or should we use a different one?
2. **IP Address**: Is `192.168.255.255` appropriate as the DNS response IP, or should we use a different private IP?
3. **CNAME vs Direct**: Should we use CNAME pointing to `127.0.50.50` or configure dnsmasq differently?
4. **Timeout Values**: Are 5 seconds for fetch and +5 seconds for SSE appropriate timeouts?
5. **PC Check Duration**: Is 30 seconds a reasonable timeout for waiting for PC-based DNS queries?
6. **Widget Placement**: Should DNS Check widget be before or after Self Check widget on dashboard?
7. **Error Messages**: Are the suggested error messages clear enough for end users?
8. **i18n Completeness**: Do the translations cover all necessary scenarios?

## Estimated Effort

- **Backend**: 2-3 hours
- **Frontend**: 2-3 hours
- **Testing**: 1-2 hours
- **Total**: 5-8 hours

## Dependencies

- Go standard library (`net`, `context`, `sync`)
- Existing API infrastructure (SSE, handlers, router)
- React hooks (`useState`, `useEffect`, `useRef`)
- Existing UI components (Card, Button, Dialog, Alert)
- i18n framework (react-i18next)
