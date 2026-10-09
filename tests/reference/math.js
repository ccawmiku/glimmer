/**
 * Pure math utilities — no side effects, no DOM, no imports.
 */

// ── Activation functions ───────────────────────────────────────────────────

export const sigmoid   = x => 1 / (1 + Math.exp(-x));
export const relu      = x => Math.max(0, x);
export const tanh      = x => Math.tanh(x);
export const leakyRelu = x => (x > 0 ? x : x * 0.08);
export const swish     = x => x * sigmoid(x);

// ── Numeric helpers ────────────────────────────────────────────────────────

export const clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, x));
export const lerp  = (a, b, t)   => a + (b - a) * t;

// ── Random number generators ───────────────────────────────────────────────

/** Uniform in [-scale, +scale] */
export function randUniform(scale = 1) {
    return (Math.random() * 2 - 1) * scale;
}

/** Box-Muller normal distribution (mean 0, std 1) */
export function randNormal() {
    const u = Math.max(Math.random(), 1e-12);
    const v = Math.max(Math.random(), 1e-12);
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
}

/** Random 32-bit unsigned seed (never 0) */
export function createCanvasSeed() {
    return (Math.floor(Math.random() * 0xFFFFFFFF) || 1) >>> 0;
}

// ── Deterministic hashing ─────────────────────────────────────────────────

/** FNV-1a hash of a string, returned as uint32 */
export function hashString(value) {
    const str = String(value);
    let h = 2166136261;
    for (let i = 0; i < str.length; i++) {
        h ^= str.charCodeAt(i);
        h  = Math.imul(h, 16777619);
    }
    return h >>> 0;
}

/** Avalanche-mix a uint32 hash into [0, 1) */
export function unitFromHash(hash) {
    let x = hash >>> 0;
    x ^= x >>> 16;
    x  = Math.imul(x, 2246822507);
    x ^= x >>> 13;
    x  = Math.imul(x, 3266489909);
    x ^= x >>> 16;
    return (x >>> 0) / 4294967296;
}

// ── Color helpers ─────────────────────────────────────────────────────────

/** Parse "#rrggbb" → { r, g, b } */
export function hexToRgb(hex) {
    const v = hex.replace('#', '');
    return {
        r: parseInt(v.slice(0, 2), 16),
        g: parseInt(v.slice(2, 4), 16),
        b: parseInt(v.slice(4, 6), 16),
    };
}

/** Viridis-like perceptual colour map: t ∈ [0,1] → rgba string */
export function scientificColor(t, alpha = 1) {
    const stops = ['#440154', '#414487', '#2A788E', '#22A884', '#7AD151', '#FDE725'].map(hexToRgb);
    const x = clamp(t, 0, 1) * (stops.length - 1);
    const i = Math.min(stops.length - 2, Math.floor(x));
    const f = x - i;
    const a = stops[i];
    const b = stops[i + 1];
    return `rgba(${Math.round(lerp(a.r, b.r, f))},` +
           `${Math.round(lerp(a.g, b.g, f))},` +
           `${Math.round(lerp(a.b, b.b, f))},${alpha})`;
}

// ── Assertion helper (used by self-tests / unit tests) ────────────────────

export function assert(condition, message) {
    if (!condition) throw new Error(`Self-test failed: ${message}`);
}
