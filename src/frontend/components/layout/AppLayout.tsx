import { ReactNode } from 'react';
import { Header } from './Header';
import { Separator } from '../ui/separator';

interface AppLayoutProps {
  children: ReactNode;
}

export function AppLayout({ children }: AppLayoutProps) {
  return (
    <div className="min-h-screen flex flex-col">
      <Header />
      <Separator />
      <main className="flex-1 container py-6">
        {children}
      </main>
    </div>
  );
}
