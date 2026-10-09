/**
 * Linear renderer — classic vertical + horizontal stripe composition.
 *
 * Also exports the lower-level helpers (generateStripes, drawLinearParts) so
 * other renderers and self-tests can reuse them.
 */

import { clamp } from '../math.js';
import { STRIPE_DENSITY_SCALE, STRIPE_BACKGROUND } from '../constants.js';
import { registerRenderer } from './registry.js';
import { sortPartsForLayerMode } from './sort.js';

// ── Stripe generation ─────────────────────────────────────────────────────

/**
 * Generate an array of stripe descriptors that cover [0, span).
 *
 * @param {'vertical'|'horizontal'} dir
 * @param {number} span           - canvas width (v) or height (h) in pixels
 * @param {{ density, widthMean, widthJitter, skipRate, minWidth, clusterBias, zBias }} config
 * @param {function(salt:string):string} pickColor
 * @param {{ whitespaceBias, zSpread, globalZBias }} globalConfig
 * @param {function(salt:string):number|null} sampler
 * @param {number} [cellSize=1]   - when >1, snap stripes to a structural grid:
 *   every stripe (and the gap a skipped stripe leaves) spans whole cells, so
 *   the parts double as clean metadata for structural transitions (wipe).
 *   Default 1 = original pixel-aligned output.
 * @returns {Array<{dir, pos, size, color, z}>}
 */
export function generateStripes(dir, span, config, pickColor, globalConfig, sampler = null, cellSize = 1) {
    const stripes      = [];
    const combinedSkip = clamp(config.skipRate + globalConfig.whitespaceBias, 0, 0.42);
    const CELL         = Math.max(1, cellSize | 0);
    let current        = 0;
    let stripeIndex    = 0;

    while (current < span) {
        const widthNoise = sampler ? sampler(`width:${stripeIndex}`) : Math.random();
        const sampled    = span * config.density * config.widthMean * (
            0.35 + Math.pow(widthNoise, 1 + config.clusterBias) * config.widthJitter
        );

        let startPx;
        let endPx;
        let nextCurrent;
        if (CELL > 1) {
            const minWidthPx = Math.max(CELL, Math.round(span * config.minWidth / CELL) * CELL);
            const startCell  = Math.ceil(current / CELL);
            startPx          = startCell * CELL;
            const cells      = Math.max(1, Math.ceil(Math.max(sampled, minWidthPx) / CELL));
            endPx            = Math.min(span, startPx + cells * CELL);
            if (endPx <= startPx) endPx = startPx + CELL;
            nextCurrent = endPx;
        } else {
            const minWidthPx = Math.max(1, Math.round(span * config.minWidth));
            const rawEnd     = Math.min(span, current + Math.max(sampled, minWidthPx));
            startPx          = Math.round(current);
            endPx            = Math.round(rawEnd);
            if (endPx <= startPx) endPx = startPx + 1;
            nextCurrent = rawEnd;
        }

        const stripeSize = endPx - startPx;
        if (stripeSize <= 0) break;

        const keepNoise = sampler ? sampler(`keep:${stripeIndex}`) : Math.random();
        if (keepNoise > combinedSkip) {
            stripes.push({
                dir,
                pos:   startPx,
                size:  stripeSize,
                color: pickColor(`color:${stripeIndex}`),
                z:     (sampler ? sampler(`z:${stripeIndex}`) : Math.random())
                       * globalConfig.zSpread + config.zBias + globalConfig.globalZBias,
            });
        }

        current = nextCurrent;
        stripeIndex++;
    }

    return stripes;
}

// ── Drawing ───────────────────────────────────────────────────────────────

export function drawLinearParts(ctx, w, h, parts, params) {
    ctx.globalAlpha = 1;
    for (const s of parts) {
        ctx.fillStyle = s.color;
        if (s.dir === 'vertical') {
            ctx.fillRect(Math.round(s.pos), 0,
                         Math.max(1, Math.round(s.size + params.edgeExpand)), h);
        } else {
            ctx.fillRect(0, Math.round(s.pos),
                         w, Math.max(1, Math.round(s.size + params.edgeExpand)));
        }
    }
}

// ── Renderer ──────────────────────────────────────────────────────────────

export const linearRenderer = {
    label: 'STRIPES',

    /** @param {CanvasRenderingContext2D} ctx @param {import('./base.js').RenderContext} c */
    render(ctx, c) {
        const { params, axisMode, layerMode, vSampler, hSampler, vPickColor, hPickColor, paletteColors, cellSize = 1 } = c;
        const w = ctx.canvas.width;
        const h = ctx.canvas.height;

        ctx.globalAlpha = 1;
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = STRIPE_BACKGROUND;
        ctx.fillRect(0, 0, w, h);

        const vConfig = {
            density:     params.vDensity * STRIPE_DENSITY_SCALE * (0.75 + params.familyBalance * 0.5),
            widthMean:   params.vWidthMean,
            widthJitter: params.vWidthJitter,
            skipRate:    params.vSkipRate,
            minWidth:    params.vMinWidth,
            clusterBias: params.vClusterBias,
            zBias:       params.vZBias,
        };
        const hConfig = {
            density:     params.hDensity * STRIPE_DENSITY_SCALE * (1.25 - params.familyBalance * 0.5),
            widthMean:   params.hWidthMean,
            widthJitter: params.hWidthJitter,
            skipRate:    params.hSkipRate,
            minWidth:    params.hMinWidth,
            clusterBias: params.hClusterBias,
            zBias:       params.hZBias,
        };
        const gConfig = { whitespaceBias: params.whitespaceBias, zSpread: params.zSpread, globalZBias: params.globalZBias };

        const vStripes = generateStripes('vertical',   w, vConfig, vPickColor, gConfig, vSampler, cellSize);
        const hStripes = generateStripes('horizontal', h, hConfig, hPickColor, gConfig, hSampler, cellSize);

        let parts = [];
        if (axisMode === 'both' || axisMode === 'x') parts = parts.concat(vStripes);
        if (axisMode === 'both' || axisMode === 'y') parts = parts.concat(hStripes);

        if (parts.length < 3) {
            if (axisMode === 'both' || axisMode === 'x')
                parts.push({ dir: 'vertical',   pos: 0,                 size: Math.round(w * 0.28), color: paletteColors[0], z: 0 });
            if (axisMode === 'both' || axisMode === 'y')
                parts.push({ dir: 'horizontal', pos: Math.round(h * 0.2), size: Math.round(h * 0.38), color: paletteColors[6], z: 1 });
        }

        sortPartsForLayerMode(parts, layerMode);
        drawLinearParts(ctx, w, h, parts, params);

        // Structural parts (draw order) + a full-frame painter — for the wipe
        return {
            parts,
            drawFrame: (dctx, dw, dh, dp) => {
                dctx.fillStyle = STRIPE_BACKGROUND;
                dctx.fillRect(0, 0, dw, dh);
                drawLinearParts(dctx, dw, dh, dp, params);
            },
        };
    },
};

registerRenderer('linear', linearRenderer);
