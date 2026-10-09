/**
 * Palette registry — the primary extension point for colour palettes.
 *
 * Usage (in palettes/index.js):
 *   import { registerPalette } from './registry.js';
 *   import { classicPalette }  from './classic.js';
 *   registerPalette('classic', classicPalette);
 *
 * Adding a new palette requires only:
 *   1. Create src/js/palettes/myPalette.js that self-registers.
 *   2. Add one import line to src/js/palettes/index.js.
 *   The UI button appears automatically.
 *
 * Contract: every palette must have exactly 32 colours (indices 0-31 match
 * the decoder's energy slots).
 */

/** @type {Map<string, {label: string, colors: string[]}>} */
const _palettes  = new Map();
let   _activeName = 'classic';

/**
 * Register a palette.
 * @param {string} name   - Unique key used in state serialization
 * @param {{ label: string, colors: string[] }} palette
 */
export function registerPalette(name, palette) {
    if (!name || typeof palette.label !== 'string') {
        throw new Error(`Palette "${name}" must have a string label`);
    }
    if (!Array.isArray(palette.colors) || palette.colors.length !== 32) {
        throw new Error(`Palette "${name}" must have exactly 32 colors (got ${palette.colors?.length})`);
    }
    _palettes.set(name, palette);
}

/** @returns {{ label: string, colors: string[] }} */
export function getPalette(name) {
    return _palettes.get(name) ?? _palettes.get('classic') ?? { label: 'CLASSIC', colors: Array(32).fill('#000000') };
}

/** @returns {string[]} Ordered list of registered palette names */
export function listPalettes() {
    return [..._palettes.keys()];
}

export function setActivePaletteName(name) {
    if (_palettes.has(name)) _activeName = name;
}

export function getActivePaletteName() {
    return _activeName;
}

export function getActivePalette() {
    return getPalette(_activeName);
}
