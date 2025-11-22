import { Badge } from '../ui/badge';

interface BadgeListProps {
  items: string[];
  variant?: 'default' | 'secondary' | 'outline' | 'destructive';
  onClick?: (item: string) => void;
}

export function BadgeList({ items, variant = 'secondary', onClick }: BadgeListProps) {
  if (!items || items.length === 0) {
    return <span className="text-sm text-muted-foreground">-</span>;
  }

  return (
    <div className="flex flex-wrap gap-1">
      {items.map((item) => (
        <Badge
          key={item}
          variant={variant}
          className={onClick ? 'cursor-pointer hover:bg-primary/20' : undefined}
          onClick={() => onClick?.(item)}
        >
          {item}
        </Badge>
      ))}
    </div>
  );
}
