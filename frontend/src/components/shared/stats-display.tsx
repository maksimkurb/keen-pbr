export function StatsDisplay({
  totalHosts,
  ipv4Subnets,
  ipv6Subnets,
}: {
  totalHosts: number | string
  ipv4Subnets: number | string
  ipv6Subnets: number | string
}) {
  return (
    <span className="text-sm text-muted-foreground">
      {totalHosts} / {ipv4Subnets} / {ipv6Subnets}
    </span>
  )
}
