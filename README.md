<div align="center">
  <img src="docs/static/logo.svg" alt="keen-pbr logo" width="140" />

  <h1>keen-pbr</h1>

  <p><strong>Policy-based routing for Linux routers.</strong></p>
  <p>Route selected traffic through VPN, WAN, or custom IP tables on <strong>OpenWrt</strong>, <strong>Keenetic</strong>, and <strong>Debian</strong> systems.</p>

  <p>
    <a href="https://github.com/maksimkurb/keen-pbr/actions/workflows/build-ci.yml">
      <img src="https://img.shields.io/github/actions/workflow/status/maksimkurb/keen-pbr/.github%2Fworkflows%2Fci-packages.yml?style=for-the-badge&branch=main" alt="Build status" />
    </a>
    <a href="https://github.com/maksimkurb/keen-pbr/releases">
      <img src="https://img.shields.io/github/v/release/maksimkurb/keen-pbr?style=for-the-badge&sort=date" alt="Latest release" />
    </a>
    <br>
    <a href="https://keen-pbr.fyi/">
      <img src="https://img.shields.io/badge/docs-keen--pbr.fyi-0f766e?style=for-the-badge" alt="Documentation" />
    </a>
    <a href="https://t.me/keen_pbr">
      <img src="https://img.shields.io/badge/Telegram-Community-229ED9?style=for-the-badge&logo=telegram&logoColor=white" alt="Telegram community" />
    </a>
  </p>
</div>

> keen-pbr is an independent open-source project. It is not an official Keenetic product and is not affiliated with or endorsed by Keenetic or Netcraze.

## What It Does

keen-pbr is a policy-based routing daemon that selectively sends traffic through specific outbounds based on domain lists, IP ranges, ports, and addresses.

It is built for embedded Linux routers and can:

- route traffic through VPN, WAN, blackhole, or custom routing tables
- use failover chains and health checks to keep traffic on healthy outbounds
- integrate with `dnsmasq` for domain-based routing
- provide an optional web UI and HTTP API for management and diagnostics

## Documentation & Installation

### <a href="https://keen-pbr.fyi/">&gt; Documentation in English &lt;</a>
### <a href="https://keen-pbr.fyi/ru/">&gt; Документация на русском &lt;</a>

## Upgrade from keen-pbr 2.x

* [Upgrade from 2.x (in English)](http://keen-pbr.fyi/docs/getting-started/upgrade-from-2x/)
* [Обновление с версии 2.x (на русском)](http://keen-pbr.fyi/ru/docs/getting-started/upgrade-from-2x/)


## Community

| Need | Link |
|---|---|
| Telegram chat | https://t.me/keen_pbr |
| Bug reports and feature requests | https://github.com/maksimkurb/keen-pbr/issues |

## Support The Project

If keen-pbr helps you, you can support its development here:

- Ko-fi: https://ko-fi.com/keen_pbr
- CloudTips: https://pay.cloudtips.ru/p/a633c47d

## License

See [LICENSE](LICENSE).
