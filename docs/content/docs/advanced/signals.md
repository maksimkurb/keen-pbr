---
title: Signals
weight: 3
---

| Signal | Action |
|---|---|
| `SIGUSR1` | Re-verify routing tables and trigger immediate URL tests |
| `SIGHUP` | Full reload: re-download lists, re-apply firewall and routing rules |
| `SIGTERM` / `SIGINT` | Graceful shutdown |

Example full reload via signal:

```bash {filename="bash"}
kill -HUP $(cat /var/run/keen-pbr.pid)
```
