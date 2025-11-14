import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Layout from './components/Layout';
import Dashboard from './pages/Dashboard';
import Rules from './pages/Rules';
import OutboundTables from './pages/OutboundTables';
import Configuration from './pages/Configuration';

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Layout />}>
          <Route index element={<Dashboard />} />
          <Route path="rules" element={<Rules />} />
          <Route path="outbound-tables" element={<OutboundTables />} />
          <Route path="config" element={<Configuration />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}

export default App;
