import type * as React from 'react';
import { cn } from '../../lib/utils';
import { useMediaQuery } from '../../src/hooks/useMediaQuery';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from './dialog';
import {
  Drawer,
  DrawerClose,
  DrawerContent,
  DrawerDescription,
  DrawerFooter,
  DrawerHeader,
  DrawerTitle,
} from './drawer';

interface ResponsiveDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  children: React.ReactNode;
}

export function ResponsiveDialog({
  open,
  onOpenChange,
  children,
}: ResponsiveDialogProps) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return (
      <Dialog open={open} onOpenChange={onOpenChange}>
        {children}
      </Dialog>
    );
  }

  return (
    <Drawer open={open} onOpenChange={onOpenChange}>
      {children}
    </Drawer>
  );
}

export function ResponsiveDialogContent({
  className,
  children,
  ...props
}: React.ComponentPropsWithoutRef<typeof DialogContent>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return (
      <DialogContent className={className} {...props}>
        {children}
      </DialogContent>
    );
  }

  return (
    <DrawerContent className={cn(className, 'w-full max-w-full')} {...props}>
      {children}
    </DrawerContent>
  );
}

export function ResponsiveDialogHeader({
  className,
  ...props
}: React.HTMLAttributes<HTMLDivElement>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogHeader className={className} {...props} />;
  }

  return <DrawerHeader className={`text-left ${className || ''}`} {...props} />;
}

export function ResponsiveDialogTitle({
  className,
  ...props
}: React.ComponentPropsWithoutRef<typeof DialogTitle>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogTitle className={className} {...props} />;
  }

  return <DrawerTitle className={className} {...props} />;
}

export function ResponsiveDialogDescription({
  className,
  ...props
}: React.ComponentPropsWithoutRef<typeof DialogDescription>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogDescription className={className} {...props} />;
  }

  return <DrawerDescription className={className} {...props} />;
}

export function ResponsiveDialogFooter({
  className,
  ...props
}: React.HTMLAttributes<HTMLDivElement>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogFooter className={className} {...props} />;
  }

  return <DrawerFooter className={className} {...props} />;
}

// Re-export DrawerClose for use in mobile drawer footers
export { DrawerClose };
