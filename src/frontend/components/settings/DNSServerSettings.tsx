import { useTranslation } from 'react-i18next';
import {
  Card,
  CardHeader,
  CardTitle,
  CardDescription,
  CardContent,
} from '../ui/card';
import {
  Field,
  FieldLabel,
  FieldDescription,
  FieldGroup,
  FieldSeparator,
} from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { InterfaceSelector } from '../ui/interface-selector';
import { StringArrayInput } from '../ui/string-array-input';
import type { DNSServerConfig } from '../../src/api/generated-types';

interface DNSServerSettingsProps {
  dnsServer: DNSServerConfig | null | undefined;
  onChange: (dnsServer: DNSServerConfig) => void;
}

export function DNSServerSettings({ dnsServer, onChange }: DNSServerSettingsProps) {
  const { t } = useTranslation();

  const updateDNSServer = (updates: Partial<DNSServerConfig>) => {
    if (!dnsServer) return;
    onChange({ ...dnsServer, ...updates });
  };

  if (!dnsServer) {
    return null;
  }

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('settings.dnsServerTitle')}</CardTitle>
        <CardDescription>
          {t('settings.dnsServerDescription')}
        </CardDescription>
      </CardHeader>

      <CardContent>
        <FieldGroup>
          {/* Enable DNS Server */}
          <Field orientation="horizontal">
            <div className="flex items-center space-x-3">
              <Checkbox
                id="dns_server_enable"
                checked={dnsServer.enable}
                onCheckedChange={(checked) =>
                  updateDNSServer({ enable: !!checked })
                }
              />
              <FieldLabel
                htmlFor="dns_server_enable"
                className="cursor-pointer flex flex-col items-start gap-0"
              >
                {t('settings.enableDnsServer')}
                <FieldDescription>
                  {t('settings.enableDnsServerDescription')}
                </FieldDescription>
              </FieldLabel>
            </div>
          </Field>

          {/* DNS Listen Address */}
          <Field>
            <FieldLabel htmlFor="dns_listen_addr">
              {t('settings.dnsListenAddr')}
            </FieldLabel>
            <FieldDescription>
              {t('settings.dnsListenAddrDescription')}
            </FieldDescription>
            <Input
              id="dns_listen_addr"
              type="text"
              value={dnsServer.listen_addr}
              onChange={(e) =>
                updateDNSServer({ listen_addr: e.target.value })
              }
              placeholder={t('settings.dnsListenAddrPlaceholder')}
              disabled={!dnsServer.enable}
            />
          </Field>

          {/* DNS Listen Port */}
          <Field>
            <FieldLabel htmlFor="dns_listen_port">
              {t('settings.dnsListenPort')}
            </FieldLabel>
            <FieldDescription>
              {t('settings.dnsListenPortDescription')}
            </FieldDescription>
            <Input
              id="dns_listen_port"
              type="number"
              min="1"
              max="65535"
              value={dnsServer.listen_port}
              onChange={(e) => {
                const value = parseInt(e.target.value, 10) || 15353;
                updateDNSServer({ listen_port: value });
              }}
              placeholder={t('settings.dnsListenPortPlaceholder')}
              disabled={!dnsServer.enable}
            />
          </Field>

          <FieldSeparator />

          {/* DNS Upstreams */}
          <Field>
            <FieldLabel>{t('settings.dnsUpstreamServers')}</FieldLabel>
            <FieldDescription>
              {t('settings.dnsUpstreamServersDescription')}
              <ul className="list-disc list-inside mt-1 space-y-0.5 text-xs">
                {(
                  t('settings.dnsFormats', {
                    returnObjects: true,
                  }) as string[]
                ).map((format) => (
                  <li key={format}>
                    <code>{format}</code>
                  </li>
                ))}
              </ul>
              <div className="mt-2">
                {t('settings.dnsUpstreamAdditionalInfo')}
              </div>
            </FieldDescription>
            <StringArrayInput
              value={dnsServer.upstreams}
              onChange={(upstreams) =>
                updateDNSServer({ upstreams })
              }
              placeholder={t('settings.dnsUpstreamPlaceholder')}
              disabled={!dnsServer.enable}
              minItems={1}
              addButtonLabel={t('settings.addUpstream')}
            />
          </Field>

          <FieldSeparator />

          {/* DNS Cache Max Domains */}
          <Field>
            <FieldLabel htmlFor="dns_cache_max_domains">
              {t('settings.dnsCacheMaxDomains')}
            </FieldLabel>
            <FieldDescription>
              {t('settings.dnsCacheMaxDomainsDescription')}
            </FieldDescription>
            <Input
              id="dns_cache_max_domains"
              type="number"
              min="100"
              value={dnsServer.cache_max_domains}
              onChange={(e) => {
                const value = parseInt(e.target.value, 10) || 1000;
                updateDNSServer({ cache_max_domains: value });
              }}
              placeholder={t('settings.dnsCacheMaxDomainsPlaceholder')}
              disabled={!dnsServer.enable}
            />
          </Field>

          {/* Drop AAAA */}
          <Field orientation="horizontal">
            <div className="flex items-center space-x-3">
              <Checkbox
                id="drop_aaaa"
                checked={dnsServer.drop_aaaa}
                onCheckedChange={(checked) =>
                  updateDNSServer({ drop_aaaa: !!checked })
                }
                disabled={!dnsServer.enable}
              />
              <FieldLabel
                htmlFor="drop_aaaa"
                className="cursor-pointer flex flex-col items-start gap-0"
              >
                {t('settings.dropAAAA')}
                <FieldDescription>
                  {t('settings.dropAAAADescription')}
                </FieldDescription>
              </FieldLabel>
            </div>
          </Field>

          {/* IPSet Entry Additional TTL */}
          <Field>
            <FieldLabel htmlFor="ipset_entry_additional_ttl_sec">
              {t('settings.ipsetEntryAdditionalTTL')}
            </FieldLabel>
            <FieldDescription>
              {t('settings.ipsetEntryAdditionalTTLDescription')}
            </FieldDescription>
            <Input
              id="ipset_entry_additional_ttl_sec"
              type="number"
              min="0"
              max="2147483"
              value={dnsServer.ipset_entry_additional_ttl_sec}
              onChange={(e) => {
                const value = parseInt(e.target.value, 10) || 0;
                updateDNSServer({ ipset_entry_additional_ttl_sec: value });
              }}
              placeholder="7200"
              disabled={!dnsServer.enable}
            />
          </Field>

          {/* Listed Domains DNS Cache TTL */}
          <Field>
            <FieldLabel htmlFor="listed_domains_dns_cache_ttl_sec">
              {t('settings.listedDomainsDNSCacheTTL')}
            </FieldLabel>
            <FieldDescription>
              {t('settings.listedDomainsDNSCacheTTLDescription')}
            </FieldDescription>
            <Input
              id="listed_domains_dns_cache_ttl_sec"
              type="number"
              min="0"
              max="2147483"
              value={dnsServer.listed_domains_dns_cache_ttl_sec}
              onChange={(e) => {
                const value = parseInt(e.target.value, 10) || 0;
                updateDNSServer({ listed_domains_dns_cache_ttl_sec: value });
              }}
              placeholder="30"
              disabled={!dnsServer.enable}
            />
          </Field>

          <FieldSeparator />

          {/* DNS Remap 53 Interfaces */}
          <Field>
            <FieldLabel>{t('settings.dnsRemap53Interfaces')}</FieldLabel>
            <FieldDescription>
              {t('settings.dnsRemap53InterfacesDescription')}
            </FieldDescription>
            <InterfaceSelector
              value={dnsServer.remap_53_interfaces}
              onChange={(interfaces) =>
                updateDNSServer({ remap_53_interfaces: interfaces })
              }
              allowReorder={false}
            />
          </Field>
        </FieldGroup>
      </CardContent>
    </Card>
  );
}
