import * as React from 'react';
import { useMediaQuery } from '../../src/hooks/useMediaQuery';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from './dialog';
import {
  Drawer,
  DrawerContent,
  DrawerDescription,
  DrawerHeader,
  DrawerTitle,
  DrawerFooter,
} from './drawer';

interface ResponsiveDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  children: React.ReactNode;
}

export function ResponsiveDialog({ open, onOpenChange, children }: ResponsiveDialogProps) {
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

export function ResponsiveDialogContent({ className, children, ...props }: React.ComponentPropsWithoutRef<typeof DialogContent>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return (
      <DialogContent className={className} {...props}>
        {children}
      </DialogContent>
    );
  }

  return (
    <DrawerContent className={className} {...props}>
      <div className="max-h-[85vh] overflow-y-auto px-4 pb-4">
        {children}
      </div>
    </DrawerContent>
  );
}

export function ResponsiveDialogHeader({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogHeader className={className} {...props} />;
  }

  return <DrawerHeader className={className} {...props} />;
}

export function ResponsiveDialogTitle({ className, ...props }: React.ComponentPropsWithoutRef<typeof DialogTitle>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogTitle className={className} {...props} />;
  }

  return <DrawerTitle className={className} {...props} />;
}

export function ResponsiveDialogDescription({ className, ...props }: React.ComponentPropsWithoutRef<typeof DialogDescription>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogDescription className={className} {...props} />;
  }

  return <DrawerDescription className={className} {...props} />;
}

export function ResponsiveDialogFooter({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  const isDesktop = useMediaQuery('(min-width: 768px)');

  if (isDesktop) {
    return <DialogFooter className={className} {...props} />;
  }

  return <DrawerFooter className={className} {...props} />;
}
