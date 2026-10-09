/**
 * Renderer registry — the primary extension point for rendering strategies.
 *
 * Usage (in renderers/index.js):
 *   import { registerRenderer } from './registry.js';
 *   import { linearRenderer }   from './linear.js';
 *   registerRenderer('linear', linearRenderer);
 *
 * Adding a new structure (e.g. a pixel grid) requires only:
 *   1. Create src/js/renderers/myRenderer.js that self-registers.
 *   2. Add one import line to src/js/renderers/index.js.
 *   The UI button appears automatically.
 */

import { assertRenderer } from './base.js';

/** @type {Map<string, import('./base.js').Renderer>} */
const _renderers  = new Map();
let   _activeName = 'linear';

/**
 * @param {string} name
 * @param {import('./base.js').Renderer} renderer
 */
export function registerRenderer(name, renderer) {
    assertRenderer(name, renderer);
    _renderers.set(name, renderer);
}

/** @returns {import('./base.js').Renderer} Falls back to 'linear' if unknown */
export function getRenderer(name) {
    return _renderers.get(name) ?? _renderers.get('linear');
}

/** @returns {string[]} */
export function listRenderers() {
    return [..._renderers.keys()];
}

export function setActiveRendererName(name) {
    if (_renderers.has(name)) _activeName = name;
}

export function getActiveRendererName() {
    return _activeName;
}

/** @returns {import('./base.js').Renderer} */
export function getActiveRenderer() {
    return getRenderer(_activeName);
}
