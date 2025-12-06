import * as Sentry from '@sentry/react';
import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';

// Initialize Sentry for error tracking
Sentry.init({
  dsn: 'https://b08196025c344c4784e3e8220de1d7e3@o175530.ingest.us.sentry.io/4510388317388800',
  // Disable default PII data collection for privacy
  sendDefaultPii: false,
});

const rootEl = document.getElementById('root');
if (rootEl) {
  const root = ReactDOM.createRoot(rootEl);
  root.render(
    <React.StrictMode>
      <App />
    </React.StrictMode>,
  );
}
