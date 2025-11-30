
interface StatsDisplayProps {
  totalHosts: number | null;
  ipv4Subnets: number | null;
  ipv6Subnets: number | null;
}

export function StatsDisplay({
  totalHosts,
  ipv4Subnets,
  ipv6Subnets,
}: StatsDisplayProps) {

  const formatStat = (value: number | null) => {
    return value !== null ? value.toString() : '-';
  };

  return (
    <span className="text-sm text-muted-foreground">
      {formatStat(totalHosts)} / {formatStat(ipv4Subnets)} /{' '}
      {formatStat(ipv6Subnets)}
    </span>
  );
}
