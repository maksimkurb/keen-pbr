import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { useEffect } from 'react';
import { HashRouter, Route, Routes } from 'react-router-dom';
import { AppLayout } from '../components/layout/AppLayout';
import { Toaster } from '../components/ui/sonner';
import Dashboard from './pages/Dashboard';
import GeneralSettings from './pages/GeneralSettings';
import ListPage from './pages/ListPage';
import Lists from './pages/Lists';
import RoutingRules from './pages/RoutingRules';
import RulePage from './pages/RulePage';
import './i18n';
import './App.css';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: 1,
      refetchOnWindowFocus: false,
    },
  },
});

const App = () => {
  // Apply dark mode based on system preference
  useEffect(() => {
    const applyTheme = (isDark: boolean) => {
      if (isDark) {
        document.documentElement.classList.add('dark');
      } else {
        document.documentElement.classList.remove('dark');
      }
    };

    // Check initial preference
    const darkModeQuery = window.matchMedia('(prefers-color-scheme: dark)');
    applyTheme(darkModeQuery.matches);

    // Listen for changes in system preference
    const handleChange = (e: MediaQueryListEvent) => {
      applyTheme(e.matches);
    };

    darkModeQuery.addEventListener('change', handleChange);

    return () => {
      darkModeQuery.removeEventListener('change', handleChange);
    };
  }, []);

  return (
    <QueryClientProvider client={queryClient}>
      <HashRouter>
        <AppLayout>
          <Routes>
            <Route path="/" element={<Dashboard />} />
            <Route path="/settings" element={<GeneralSettings />} />
            <Route path="/lists" element={<Lists />} />
            <Route path="/lists/new" element={<ListPage />} />
            <Route path="/lists/:name/edit" element={<ListPage />} />
            <Route path="/routing-rules" element={<RoutingRules />} />
            <Route path="/routing-rules/new" element={<RulePage />} />
            <Route path="/routing-rules/:name/edit" element={<RulePage />} />
          </Routes>
        </AppLayout>
      </HashRouter>
      <Toaster />
    </QueryClientProvider>
  );
};

export default App;
