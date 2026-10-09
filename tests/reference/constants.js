/**
 * Global constants shared across all modules. Never mutated at runtime.
 */

/** Latent vector dimension */
export const LATENT_DIM = 192;
/** First hidden layer width */
export const HIDDEN_1   = 96;
/** Second hidden layer width */
export const HIDDEN_2   = 64;
/** Decoder output dimension */
export const OUT_DIM    = 64;

// Frame rate constants are exported for the perf harness / legacy globals.
// The animation loop itself is switch-timer driven (see main.js), not FPS driven.
export const FPS_LIMIT           = 8;
export const FRAME_DELAY         = 1000 / FPS_LIMIT;
export const MAX_TRAIL_POINTS    = 240;
/** localStorage key — bump suffix only when state schema breaks backward compat */
export const STORAGE_KEY         = 'latent-art-canvas-v3-state';
export const STRIPE_DENSITY_SCALE = 0.62;
export const TAU                 = Math.PI * 2;
/** Background fill of the STRIPES renderer — the wipe's bottom layer */
export const STRIPE_BACKGROUND    = '#fafafa';
