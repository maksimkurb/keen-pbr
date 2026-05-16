import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  createAppMockConfig,
  createAppMockListRefreshState,
} from "../fixtures/app-mock-config"

export type DemoConfigState = {
  config: ConfigObject
  is_draft: boolean
  list_refresh_state: Record<
    string,
    { last_refresh_ts?: number; content_hash?: string }
  >
}

export function createInitialDemoState(): DemoConfigState {
  return {
    is_draft: false,
    list_refresh_state: createAppMockListRefreshState(),
    config: createAppMockConfig(),
  }
}

let demoState = createInitialDemoState()

export function getDemoState(): DemoConfigState {
  return demoState
}

export function replaceDemoConfig(config: ConfigObject) {
  demoState = {
    ...demoState,
    config,
    is_draft: true,
  }
}

export function applyDemoConfig() {
  demoState = {
    ...demoState,
    is_draft: false,
  }
}

export function resetDemoState() {
  demoState = createInitialDemoState()
}
