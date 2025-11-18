import { HashRouter, Routes, Route } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { AppLayout } from '../components/layout/AppLayout';
import { Toaster } from '../components/ui/sonner';
import Dashboard from './pages/Dashboard';
import GeneralSettings from './pages/GeneralSettings';
import Lists from './pages/Lists';
import RoutingRules from './pages/RoutingRules';
import './i18n';
import "./App.css";

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: 1,
      refetchOnWindowFocus: false,
    },
  },
});

const App = () => {
  return (
    <QueryClientProvider client={queryClient}>
      <HashRouter>
        <AppLayout>
          <Routes>
            <Route path="/" element={<Dashboard />} />
            <Route path="/settings" element={<GeneralSettings />} />
            <Route path="/lists" element={<Lists />} />
            <Route path="/routing-rules" element={<RoutingRules />} />
          </Routes>
        </AppLayout>
      </HashRouter>
      <Toaster />
    </QueryClientProvider>
  );
};

export default App;
