import {
  FileTextIcon,
  HomeIcon,
  Menu,
  RouteIcon,
  SettingsIcon,
  X,
} from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Link, useLocation } from 'react-router-dom';
import { cn } from '../../lib/utils';
import { LanguageSelector } from '../shared/LanguageSelector';
import { Button } from '../ui/button';

export function Header() {
  const { t } = useTranslation();
  const location = useLocation();
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);

  const navItems = [
    { path: '/', label: t('nav.dashboard'), icon: <HomeIcon /> },
    { path: '/settings', label: t('nav.settings'), icon: <SettingsIcon /> },
    { path: '/lists', label: t('nav.lists'), icon: <FileTextIcon /> },
    {
      path: '/routing-rules',
      label: t('nav.routingRules'),
      icon: <RouteIcon />,
    },
  ];

  return (
    <header className="border-b sticky">
      <div className="container max-w-7xl mx-auto px-4 flex h-16 items-center justify-between">
        <div className="flex items-center gap-4 flex-1">
          <Link
            to="/"
            className="text-xl font-bold flex flex-row gap-2 whitespace-nowrap"
          >
            <img src="/logo.svg" alt="Logo" className="h-8 rounded-sm" />
            keen-pbr
          </Link>

          {/* Desktop Navigation */}
          <nav className="hidden lg:flex gap-1">
            {navItems.map((item) => (
              <Button
                key={item.path}
                variant={location.pathname === item.path ? 'default' : 'ghost'}
                asChild
              >
                <Link to={item.path}>
                  {item.icon || null} {item.label}
                </Link>
              </Button>
            ))}
          </nav>
        </div>

        {/* Desktop Language Selector */}
        <div className="hidden lg:block">
          <LanguageSelector className="w-[120px]" />
        </div>

        {/* Mobile Menu Button */}
        <Button
          variant="ghost"
          size="sm"
          className="lg:hidden"
          onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
        >
          {mobileMenuOpen ? (
            <X className="h-5 w-5" />
          ) : (
            <Menu className="h-5 w-5" />
          )}
        </Button>
      </div>

      {/* Mobile Menu */}
      <div
        className={cn(
          `lg:hidden border-t bg-background overflow-hidden transition-all duration-300 ease-in-out`,
          mobileMenuOpen ? 'max-h-[500px] opacity-100' : 'max-h-0 opacity-0',
        )}
      >
        <nav className="container max-w-7xl mx-auto px-4 py-4 flex flex-col gap-2">
          {navItems.map((item) => (
            <Button
              key={item.path}
              variant={location.pathname === item.path ? 'default' : 'ghost'}
              className="justify-start"
              asChild
              onClick={() => setMobileMenuOpen(false)}
            >
              <Link to={item.path}>
                {item.icon || null} {item.label}
              </Link>
            </Button>
          ))}
          <LanguageSelector className="w-full" />
        </nav>
      </div>
    </header>
  );
}
