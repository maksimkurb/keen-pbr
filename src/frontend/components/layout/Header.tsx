import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Link, useLocation } from 'react-router-dom';
import { Menu, X } from 'lucide-react';
import { Button } from '../ui/button';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '../ui/select';

export function Header() {
  const { t, i18n } = useTranslation();
  const location = useLocation();
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);

  const navItems = [
    { path: '/', label: t('nav.dashboard') },
    { path: '/settings', label: t('nav.settings') },
    { path: '/lists', label: t('nav.lists') },
    { path: '/routing-rules', label: t('nav.routingRules') },
  ];

  const changeLanguage = (lng: string) => {
    i18n.changeLanguage(lng);
  };

  return (
    <header className="border-b">
      <div className="container max-w-7xl mx-auto px-4 flex h-16 items-center justify-between">
        <div className="flex items-center gap-4 flex-1">
          <h1 className="text-xl font-bold">keen-pbr</h1>

          {/* Desktop Navigation */}
          <nav className="hidden md:flex gap-1">
            {navItems.map((item) => (
              <Button
                key={item.path}
                variant={location.pathname === item.path ? 'default' : 'ghost'}
                asChild
              >
                <Link to={item.path}>{item.label}</Link>
              </Button>
            ))}
          </nav>
        </div>

        {/* Desktop Language Selector */}
        <div className="hidden md:block">
          <Select value={i18n.language} onValueChange={changeLanguage}>
            <SelectTrigger className="w-[120px]">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="en">{t('common.language.en')}</SelectItem>
              <SelectItem value="ru">{t('common.language.ru')}</SelectItem>
            </SelectContent>
          </Select>
        </div>

        {/* Mobile Menu Button */}
        <Button
          variant="ghost"
          size="sm"
          className="md:hidden"
          onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
        >
          {mobileMenuOpen ? <X className="h-5 w-5" /> : <Menu className="h-5 w-5" />}
        </Button>
      </div>

      {/* Mobile Menu */}
      <div
        className={`md:hidden border-t bg-background overflow-hidden transition-all duration-300 ease-in-out ${
          mobileMenuOpen ? 'max-h-[500px] opacity-100' : 'max-h-0 opacity-0'
        }`}
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
              <Link to={item.path}>{item.label}</Link>
            </Button>
          ))}
          <div className="pt-2 border-t mt-2">
            <Select value={i18n.language} onValueChange={changeLanguage}>
              <SelectTrigger className="w-full">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="en">{t('common.language.en')}</SelectItem>
                <SelectItem value="ru">{t('common.language.ru')}</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </nav>
      </div>
    </header>
  );
}
