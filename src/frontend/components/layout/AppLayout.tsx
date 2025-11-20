import { ReactNode } from 'react';
import { Header } from './Header';
import { Separator } from '../ui/separator';
import { ConfigurationWarning } from '../ConfigurationWarning';

interface AppLayoutProps {
  children: ReactNode;
}

export function AppLayout({ children }: AppLayoutProps) {
  return (
    <div className="min-h-screen flex flex-col">
      <Header />
      <Separator />
      {/* Global configuration warning - shows on all pages */}
      <div className="container max-w-7xl mx-auto px-4 pt-6">
        <ConfigurationWarning />
      </div>
      <main className="flex-1 w-full">
        <div className="container max-w-7xl mx-auto px-4 py-6">
          {children}
        </div>
      </main>
    </div>
  );
}
