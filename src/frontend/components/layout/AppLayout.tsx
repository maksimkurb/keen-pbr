import type { ReactNode } from 'react';
import { Header } from './Header';
import { ConfigurationWarning } from '../ConfigurationWarning';
import { Separator } from '../ui/separator';

interface AppLayoutProps {
  children: ReactNode;
}

export function AppLayout({ children }: AppLayoutProps) {
  return (
    <div className="min-h-screen flex flex-col">
      <Header />
      <Separator />
      <main className="flex-1 w-full">
        <div className="container max-w-7xl mx-auto px-4 py-6">
          <ConfigurationWarning />
          {children}
        </div>
      </main>
    </div>
  );
}
