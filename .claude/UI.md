# keen-pbr UI Implementation Plan

## Overview

This document outlines the implementation plan for the keen-pbr web UI - a single-page application for managing policy-based routing configuration through the REST API.

## Technical Stack

- **Framework**: React 19
- **Router**: React Router v6 with hash routing
- **UI Components**: shadcn/ui (Radix UI primitives)
- **Styling**: TailwindCSS v4
- **Build Tool**: Rsbuild
- **I18n**: react-i18next
- **State Management**: React Context API + hooks
- **HTTP Client**: Existing API client (`src/frontend/src/api/client.ts`)

## Architecture Principles

### Component Organization

```
src/
├── components/
│   ├── dashboard/          # Dashboard feature components
│   ├── lists/              # Lists management components
│   ├── routing-rules/      # Routing rules components
│   ├── settings/           # Settings components
│   ├── layout/             # Layout components (navigation, etc.)
│   └── shared/             # Shared/reusable components
├── pages/                  # Page-level smart components
│   ├── Dashboard.tsx
│   ├── GeneralSettings.tsx
│   ├── Lists.tsx
│   └── RoutingRules.tsx
├── hooks/                  # Custom React hooks
├── i18n/                   # Internationalization
├── contexts/               # React contexts
└── lib/                    # Utilities
```

### Design Principles

1. **No Plain HTML Elements**: All components use shadcn/ui primitives
2. **Smart vs Dumb Components**:
   - **Smart**: Pages and feature components (handle state, API calls)
   - **Dumb**: UI components (receive props, render UI)
3. **Reusability**: Extract common patterns into shared components
4. **Type Safety**: Full TypeScript coverage
5. **Responsive**: Mobile-first design
6. **Accessibility**: ARIA labels, keyboard navigation

## Navigation Structure

### Hash-based Routing

```typescript
// Routes
const routes = {
  dashboard: "#/",
  settings: "#/settings",
  lists: "#/lists",
  routingRules: "#/routing-rules"
}
```

### Layout Component

```
┌─────────────────────────────────────────────┐
│  Header (Logo, Navigation Tabs, Language)   │
├─────────────────────────────────────────────┤
│                                             │
│                                             │
│              Page Content                   │
│                                             │
│                                             │
└─────────────────────────────────────────────┘
```

## Page Specifications

### 1. Dashboard Page (`/`)

**Purpose**: System status overview and domain routing checker

#### Components

**1.1 StatusCard**
- Location: `src/components/dashboard/StatusCard.tsx`
- Props: `{ title: string, value: string, status?: 'running' | 'stopped' | 'unknown' }`
- Uses: `Card`, `Badge` from shadcn
- Displays: Version info, service status with colored badges

**1.2 ServiceStatusWidget**
- Location: `src/components/dashboard/ServiceStatusWidget.tsx`
- Data source: `GET /api/v1/status`
- Uses: `StatusCard` components in a grid
- Shows:
  - keen-pbr version
  - Keenetic OS version
  - keen-pbr service status (running/stopped)
  - dnsmasq service status (running/stopped)

**1.3 DomainCheckerWidget**
- Location: `src/components/dashboard/DomainCheckerWidget.tsx`
- Uses: `Card`, `Input`, `Button`, `Badge`, `Alert`
- Features:
  - Input field for domain/IP (using `Input` with search icon)
  - Three action buttons: "Check Routing", "Ping", "Traceroute" (using `Button`)
  - Results display:
    - Which IPSets contain this domain/IP (using `Badge` for each ipset)
    - Current routing configuration (table, fwmark, interfaces)
    - Visual status indicator

**Layout**:
```
┌─────────────────────────────────────────┐
│ System Status                           │
│ ┌───────┬───────┬────────┬────────┐   │
│ │Version│Keenetic│keen-pbr│dnsmasq│   │
│ └───────┴───────┴────────┴────────┘   │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ Domain/IP Routing Checker               │
│ ┌─────────────────────────────────────┐ │
│ │ [domain/IP input]                    │ │
│ └─────────────────────────────────────┘ │
│ [Check Routing] [Ping] [Traceroute]    │
│                                         │
│ Results: (if checked)                   │
│ • Found in IPSets: [ipset1] [ipset2]   │
│ • Route: table 100 via wg0             │
└─────────────────────────────────────────┘
```

#### shadcn Components Used
- `Card`, `CardHeader`, `CardTitle`, `CardContent`
- `Input`, `Button`, `Badge`, `Alert`, `Separator`

---

### 2. General Settings Page (`/settings`)

**Purpose**: Manage global configuration

#### Components

**2.1 SettingsForm**
- Location: `src/components/settings/SettingsForm.tsx`
- Data source: `GET /api/v1/settings`
- Mutation: `PATCH /api/v1/settings`
- Uses: `Form`, `Field`, `Input`, `Checkbox`, `Button`

**Form Fields**:
1. `lists_output_dir` - Text input for directory path
2. `api_bind_address` - Text input for bind address (optional)
3. `use_keenetic_dns` - Checkbox for enabling Keenetic DNS
4. `fallback_dns` - Text input for fallback DNS server (optional)

**Layout**:
```
┌─────────────────────────────────────────┐
│ General Settings                        │
│                                         │
│ Lists Output Directory                  │
│ [/opt/etc/keen-pbr/lists.d]             │
│                                         │
│ API Bind Address                        │
│ [0.0.0.0:8080]                          │
│                                         │
│ ☑ Use Keenetic DNS                      │
│                                         │
│ Fallback DNS Server                     │
│ [8.8.8.8]                               │
│                                         │
│              [Save Changes]             │
└─────────────────────────────────────────┘
```

#### shadcn Components Used
- `Form`, `Field`, `Label`, `Input`, `Checkbox`, `Button`
- `Card`, `Separator`, `Toaster` (for success/error notifications)

---

### 3. Lists Page (`/lists`)

**Purpose**: Manage IP/domain lists

#### Components

**3.1 ListsTable**
- Location: `src/components/lists/ListsTable.tsx`
- Data source: `GET /api/v1/lists`
- Uses: Custom data table component built with shadcn primitives

**Table Columns**:
1. **List Name** - Text with type icon
2. **Type** - Badge showing `URL`, `File`, or `Inline`
3. **Statistics** - `{hosts} / {ipv4} / {ipv6}` (null values shown as "-")
4. **Rules** - Link showing count `Rules (3) →`
5. **Actions** - Edit/Delete buttons

**3.2 ListFilters**
- Location: `src/components/lists/ListFilters.tsx`
- Uses: `Input`, `Select`
- Filters:
  - List name (text search)
  - Used in rule (dropdown, populated from IPSets)
- URL state: Filters persist in hash parameters

**3.3 CreateListDialog**
- Location: `src/components/lists/CreateListDialog.tsx`
- Mutation: `POST /api/v1/lists`
- Uses: `Dialog`, `Form`, `Input`, `Select`, `Button`, `Textarea`
- Form Fields:
  - `list_name` - Text input
  - `type` - Select (URL/File/Inline)
  - Conditional fields based on type:
    - URL: `url` input
    - File: `file` path input
    - Inline: `hosts` textarea (one per line)

**3.4 EditListDialog**
- Location: `src/components/lists/EditListDialog.tsx`
- Mutation: `PUT /api/v1/lists/{name}`
- Similar to CreateListDialog with pre-filled data

**3.5 DeleteListConfirmation**
- Location: `src/components/lists/DeleteListConfirmation.tsx`
- Mutation: `DELETE /api/v1/lists/{name}`
- Uses: `AlertDialog` from shadcn
- Shows warning if list is referenced by IPSets

**Layout**:
```
┌─────────────────────────────────────────┐
│ Lists (12)                [+ New List]  │
├─────────────────────────────────────────┤
│ Search: [        ]  Used in: [All ▼]    │
├─────────────────────────────────────────┤
│ Name      │Type│Stats    │Rules │Actions│
├───────────┼────┼─────────┼──────┼───────┤
│vpn-domains│URL │150/25/5 │3 →   │✏️ 🗑️  │
│local-ips  │File│0/100/0  │1 →   │✏️ 🗑️  │
│...        │    │         │      │       │
└─────────────────────────────────────────┘
```

**Empty State**:
```
┌─────────────────────────────────────────┐
│                                         │
│         📋                              │
│    No lists found                       │
│                                         │
│  [Create your first list]               │
│     or [Clear filters]                  │
│                                         │
└─────────────────────────────────────────┘
```

#### shadcn Components Used
- `Table`, `TableHeader`, `TableRow`, `TableCell`
- `Input`, `Select`, `Button`, `Badge`, `Dialog`
- `AlertDialog`, `Empty`, `Separator`

---

### 4. Routing Rules Page (`/routing-rules`)

**Purpose**: Manage IPSet routing configurations

#### Components

**4.1 RoutingRulesTable**
- Location: `src/components/routing-rules/RoutingRulesTable.tsx`
- Data source: `GET /api/v1/ipsets`
- Uses: Custom data table component

**Table Columns**:
1. **IPSet Name** - Text
2. **Routing** - `priority: {p}, table: {t}, mark: {m}`
3. **IP Version** - Badge showing `IPv4` or `IPv6`
4. **Lists** - Multiple badges, one per list (clickable to filter Lists page)
5. **Interfaces** - Multiple badges showing interface priority
6. **Kill Switch** - Icon/badge indicating enabled/disabled
7. **Actions** - Edit/Delete buttons

**4.2 RuleFilters**
- Location: `src/components/routing-rules/RuleFilters.tsx`
- Uses: `Input`, `Select`
- Filters:
  - IPSet name (text search)
  - Uses list (dropdown, populated from Lists)
  - IP version (dropdown: All/IPv4/IPv6)
- URL state: Filters persist in hash parameters

**4.3 CreateRuleDialog**
- Location: `src/components/routing-rules/CreateRuleDialog.tsx`
- Mutation: `POST /api/v1/ipsets`
- Uses: `Dialog`, `Form`, `Input`, `Select`, `Checkbox`, `Button`
- Form sections:
  - **Basic Info**: name, IP version
  - **Lists**: Multi-select of available lists
  - **Routing**: interfaces (multi-input), priority, table, fwmark
  - **Options**: flush_before_applying, kill_switch checkboxes
  - **DNS Override**: optional input (server#port format)

**4.4 EditRuleDialog**
- Location: `src/components/routing-rules/EditRuleDialog.tsx`
- Mutation: `PUT /api/v1/ipsets/{name}`
- Similar to CreateRuleDialog with pre-filled data

**4.5 DeleteRuleConfirmation**
- Location: `src/components/routing-rules/DeleteRuleConfirmation.tsx`
- Mutation: `DELETE /api/v1/ipsets/{name}`
- Uses: `AlertDialog`

**Layout**:
```
┌─────────────────────────────────────────────────┐
│ Routing Rules (5)             [+ New Rule]      │
├─────────────────────────────────────────────────┤
│ Search: [     ] List: [All ▼] Version: [All ▼]  │
├─────────────────────────────────────────────────┤
│Name   │Routing  │Ver│Lists│Interfaces│KS│Actions│
├───────┼─────────┼───┼─────┼──────────┼──┼───────┤
│vpn_set│100/100  │v4 │[vpn]│[wg0][nwg]│✓ │✏️ 🗑️   │
│       │fwmark100│   │     │          │  │       │
│...    │         │   │     │          │  │       │
└─────────────────────────────────────────────────┘
```

#### shadcn Components Used
- `Table`, `TableHeader`, `TableRow`, `TableCell`
- `Input`, `Select`, `Button`, `Badge`, `Dialog`
- `AlertDialog`, `Checkbox`, `Separator`

---

## Layout Components

### AppLayout

Location: `src/components/layout/AppLayout.tsx`

Uses: `Separator`

**Structure**:
```typescript
<div className="min-h-screen flex flex-col">
  <Header />
  <Separator />
  <main className="flex-1 container py-6">
    {children}
  </main>
</div>
```

### Header

Location: `src/components/layout/Header.tsx`

Uses: `Button` (for nav tabs), `Select` (for language)

**Features**:
- Logo/title
- Navigation tabs (Dashboard, Settings, Lists, Routing Rules)
- Language selector (top-right)
- Active tab highlighting

---

## Shared Components

### DataTableEmpty

Location: `src/components/shared/DataTableEmpty.tsx`

Uses: `Empty` from shadcn

Props:
```typescript
{
  title: string;
  description: string;
  action?: {
    label: string;
    onClick: () => void;
  };
  clearFilters?: () => void;
}
```

### BadgeList

Location: `src/components/shared/BadgeList.tsx`

Uses: `Badge`

Props:
```typescript
{
  items: string[];
  variant?: BadgeVariant;
  onClick?: (item: string) => void;
}
```

### StatsDisplay

Location: `src/components/shared/StatsDisplay.tsx`

Uses: `Separator`

Props:
```typescript
{
  totalHosts: number | null;
  ipv4Subnets: number | null;
  ipv6Subnets: number | null;
}
```

Renders: `{hosts} / {ipv4} / {ipv6}` with "-" for null values

---

## Internationalization (i18n)

### Setup

```typescript
// src/i18n/index.ts
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import en from './locales/en.json';
import ru from './locales/ru.json';

i18n
  .use(initReactI18next)
  .init({
    resources: {
      en: { translation: en },
      ru: { translation: ru }
    },
    lng: 'en',
    fallbackLng: 'en',
    interpolation: {
      escapeValue: false
    }
  });
```

### Translation Files Structure

```json
// src/i18n/locales/en.json
{
  "nav": {
    "dashboard": "Dashboard",
    "settings": "General Settings",
    "lists": "Lists",
    "routingRules": "Routing Rules"
  },
  "dashboard": {
    "systemStatus": "System Status",
    "version": "keen-pbr Version",
    "keeneticVersion": "Keenetic OS",
    "serviceStatus": "Service Status",
    "domainChecker": {
      "title": "Domain/IP Routing Checker",
      "placeholder": "Enter domain or IP address",
      "checkRouting": "Check Routing",
      "ping": "Ping",
      "traceroute": "Traceroute"
    }
  },
  "lists": {
    "title": "Lists",
    "newList": "New List",
    "searchPlaceholder": "Search lists...",
    "usedInFilter": "Used in rule",
    "columns": {
      "name": "List Name",
      "type": "Type",
      "stats": "Domains / IPv4 / IPv6",
      "rules": "Rules",
      "actions": "Actions"
    },
    "empty": {
      "title": "No lists found",
      "description": "Create your first list to get started",
      "createButton": "Create List",
      "clearFilters": "Clear filters"
    }
  },
  "settings": {
    "title": "General Settings",
    "listsOutputDir": "Lists Output Directory",
    "apiBindAddress": "API Bind Address",
    "useKeeneticDns": "Use Keenetic DNS",
    "fallbackDns": "Fallback DNS Server",
    "saveChanges": "Save Changes"
  },
  "routingRules": {
    "title": "Routing Rules",
    // ... similar structure
  },
  "common": {
    "save": "Save",
    "cancel": "Cancel",
    "delete": "Delete",
    "edit": "Edit",
    "create": "Create",
    "loading": "Loading...",
    "error": "Error",
    "success": "Success"
  }
}
```

### Usage in Components

```typescript
import { useTranslation } from 'react-i18next';

function MyComponent() {
  const { t } = useTranslation();

  return <h1>{t('dashboard.systemStatus')}</h1>;
}
```

---

## State Management

### API State

Use React Query (TanStack Query) for server state:

```typescript
// src/hooks/useLists.ts
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiClient } from '../api/client';

export function useLists() {
  return useQuery({
    queryKey: ['lists'],
    queryFn: () => apiClient.getLists()
  });
}

export function useCreateList() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: apiClient.createList,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['lists'] });
    }
  });
}
```

### URL State

For filters, use hash parameters:

```typescript
// src/hooks/useListFilters.ts
import { useSearchParams } from 'react-router-dom';

export function useListFilters() {
  const [searchParams, setSearchParams] = useSearchParams();

  const filters = {
    name: searchParams.get('name') || '',
    usedInRule: searchParams.get('rule') || ''
  };

  const updateFilter = (key: string, value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value) {
      newParams.set(key, value);
    } else {
      newParams.delete(key);
    }
    setSearchParams(newParams);
  };

  return { filters, updateFilter };
}
```

---

## Error Handling

### Toast Notifications

Use `sonner` (already installed) for notifications:

```typescript
import { toast } from 'sonner';

// Success
toast.success('List created successfully');

// Error
toast.error('Failed to create list', {
  description: error.message
});

// Loading
const toastId = toast.loading('Creating list...');
// Later: toast.success('Created!', { id: toastId });
```

### Error Boundary

```typescript
// src/components/shared/ErrorBoundary.tsx
import { Component, ReactNode } from 'react';
import { Alert } from '@/components/ui/alert';

interface Props {
  children: ReactNode;
}

interface State {
  hasError: boolean;
  error?: Error;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { hasError: false };

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  render() {
    if (this.state.hasError) {
      return (
        <Alert variant="destructive">
          <AlertTitle>Something went wrong</AlertTitle>
          <AlertDescription>{this.state.error?.message}</AlertDescription>
        </Alert>
      );
    }

    return this.props.children;
  }
}
```

---

## Routing Implementation

### Router Setup

```typescript
// src/App.tsx
import { HashRouter, Routes, Route } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { AppLayout } from './components/layout/AppLayout';
import Dashboard from './pages/Dashboard';
import GeneralSettings from './pages/GeneralSettings';
import Lists from './pages/Lists';
import RoutingRules from './pages/RoutingRules';
import './i18n';

const queryClient = new QueryClient();

export default function App() {
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
}
```

---

## Dependencies to Add

```json
{
  "dependencies": {
    "react-router-dom": "^7.1.1",
    "@tanstack/react-query": "^5.62.11",
    "i18next": "^24.2.0",
    "react-i18next": "^15.2.0"
  }
}
```

---

## Implementation Order

### Phase 1: Foundation
1. ✅ Install dependencies
2. ✅ Setup i18n
3. ✅ Setup React Router with hash routing
4. ✅ Setup React Query
5. ✅ Create AppLayout and Header components
6. ✅ Create basic routing structure

### Phase 2: Dashboard
1. Create StatusCard component
2. Create ServiceStatusWidget
3. Create DomainCheckerWidget
4. Wire up Dashboard page
5. Test with mock data

### Phase 3: Settings
1. Create SettingsForm component
2. Wire up API integration
3. Add validation and error handling
4. Test save functionality

### Phase 4: Lists Management
1. Create ListsTable component
2. Create ListFilters component
3. Create CreateListDialog
4. Create EditListDialog
5. Create DeleteListConfirmation
6. Wire up filtering logic
7. Test CRUD operations

### Phase 5: Routing Rules
1. Create RoutingRulesTable component
2. Create RuleFilters component
3. Create CreateRuleDialog
4. Create EditRuleDialog
5. Create DeleteRuleConfirmation
6. Wire up filtering logic
7. Test CRUD operations

### Phase 6: Polish
1. Add loading states everywhere
2. Add error boundaries
3. Add toast notifications
4. Add responsive design
5. Add accessibility features
6. Add Russian translations
7. Final testing and bug fixes

---

## Testing Strategy

### Unit Tests
- Test all utility functions
- Test custom hooks
- Test form validation logic

### Component Tests
- Test each component in isolation
- Mock API calls
- Test user interactions

### Integration Tests
- Test full user flows
- Test navigation
- Test API integration

---

## Accessibility Considerations

1. **Keyboard Navigation**: All interactive elements accessible via Tab
2. **ARIA Labels**: Proper labels for screen readers
3. **Focus Management**: Visible focus indicators
4. **Color Contrast**: WCAG AA compliance
5. **Alt Text**: Descriptive text for icons
6. **Form Labels**: Clear associations between labels and inputs

---

## Performance Optimizations

1. **Code Splitting**: Lazy load pages
2. **Memoization**: Use React.memo for expensive components
3. **Virtual Scrolling**: For large tables (if needed)
4. **Debounced Search**: For filter inputs
5. **Query Caching**: React Query automatic caching
6. **Bundle Optimization**: Tree shaking, minification

---

## Browser Support

- Chrome/Edge: Latest 2 versions
- Firefox: Latest 2 versions
- Safari: Latest 2 versions
- Mobile browsers: iOS Safari, Chrome Android

---

## Notes

- All timestamps displayed in local timezone
- All numeric inputs validated on client and server
- Forms auto-save to URL state (filters)
- Dialogs use keyboard shortcuts (Esc to close)
- Tables support sorting (if needed in future)
- Theme switching (dark/light mode) can be added later

---

*Last Updated: November 2024*
