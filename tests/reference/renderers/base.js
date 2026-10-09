/**
 * Renderer interface definition.
 *
 * Every renderer is a plain object `{ label, render }`:
 *   - label:  string shown in the UI (e.g. 'STRIPES', 'CIRCLE', 'PIXELS')
 *   - render: `(ctx, c) => void` draws one complete frame.
 *             Canvas dimensions are read from ctx.canvas.width/height.
 *
 * @typedef {Object} Renderer
 * @property {string} label
 * @property {function(CanvasRenderingContext2D, RenderContext): void} render
 */

/**
 * Context object handed to every renderer — assembled by renderer.js:drawArt().
 *
 * @typedef {Object} RenderContext
 * @property {object} params
 *   Decoded physical params from LatentEntity.getPhysicalParams()
 *   (vDensity, hDensity, vWidthMean, zSpread, vZBias/hZBias, paletteIndices, …).
 * @property {'both'|'x'|'y'} axisMode
 *   Which axes to render (x = vertical family, y = horizontal family).
 * @property {'auto'|'xTop'|'yTop'} layerMode
 *   Z-sort strategy; which axis family wins when both are active.
 * @property {function(salt:string):number} vSampler
 *   Deterministic noise sampler for the vertical axis family.
 * @property {function(salt:string):number} hSampler
 *   Deterministic noise sampler for the horizontal axis family.
 * @property {function(salt:string):string} vPickColor
 *   Colour picker bound to the vertical axis family.
 * @property {function(salt:string):string} hPickColor
 *   Colour picker bound to the horizontal axis family.
 * @property {string[]} paletteColors
 *   Full colours array of the currently active palette (indices 0-31).
 */

/**
 * Runtime conformance check for newly registered renderers.
 * @param {string} name
 * @param {*} renderer
 */
export function assertRenderer(name, renderer) {
    if (typeof renderer?.render !== 'function') {
        throw new Error(`Renderer "${name}" must export a render() function`);
    }
    if (typeof renderer.label !== 'string') {
        throw new Error(`Renderer "${name}" must have a string label`);
    }
}
