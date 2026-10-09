import { sigmoid, hashString, unitFromHash } from './math.js';

// ── Stable deterministic sampler ──────────────────────────────────────────

/**
 * Returns a pure function `(salt: string) → [0, 1)` that produces
 * deterministic, stable values tied to the current canvas and generation.
 *
 * @param {string} scope          - 'vertical' | 'horizontal' | any namespacing string
 * @param {object|null} params    - decoded render params (for tempo/phase)
 * @param {number} renderGeneration
 * @param {number} canvasSeed
 * @param {number} randomnessLevel - [0, 1]
 */
export function createStableSampler(scope, params, renderGeneration, canvasSeed, randomnessLevel) {
    const tempo     = params ? params.randomTempo      : 0.34;
    const phaseBias = params ? params.randomPhaseBias  : 0;
    const phaseWarp = params ? params.randomPhaseWarp  : 0;
    const rate      = randomnessLevel <= 0 ? 0 : Math.pow(randomnessLevel, 1.35) * tempo;
    const curved    = renderGeneration + Math.sin(renderGeneration * 0.013 + phaseBias) * phaseWarp;
    const phase     = rate <= 0 ? 0 : Math.floor(curved * rate + phaseBias * 11);
    return salt => unitFromHash(hashString(`${canvasSeed}:${scope}:${phase}:${salt}`));
}

// ── Family weight helpers ─────────────────────────────────────────────────

export function buildFamilyWeights(indices, energies, familyBias) {
    const exponent = familyBias >= 0 ? 1 + familyBias * 1.8 : 1 + familyBias * 0.6;
    const weights  = indices.map(idx => {
        const e = Math.max(energies[idx], -4);
        return Math.pow(sigmoid(e * 1.4) + 0.001, exponent);
    });
    let total = weights.reduce((a, b) => a + b, 0);
    if (total <= 0) total = 1;
    return { weights, total };
}

export function pickWeightedIndex(indices, familyWeightInfo, accentBias, randomUnit = Math.random) {
    const { weights, total } = familyWeightInfo;
    if (randomUnit() < accentBias) return indices[0];
    let r = randomUnit() * total;
    for (let i = 0; i < indices.length; i++) {
        r -= weights[i];
        if (r <= 0) return indices[i];
    }
    return indices[indices.length - 1];
}

// ── Colour picker factory ─────────────────────────────────────────────────

/**
 * Creates a deterministic colour picker bound to a palette and energy layout.
 *
 * @param {string[]} paletteColors   - Full colours array from active palette
 * @param {number[]} indices         - Active palette indices from getPhysicalParams()
 * @param {number[]} energies        - Energy for each index (0-31)
 * @param {number}   familyBias      - vPaletteBias or hPaletteBias from params
 * @param {number}   accentBias      - From params
 * @param {function(string):number|null} sampler - Stable sampler or null for Math.random
 * @returns {function(salt:string): string}
 */
export function createColorPicker(paletteColors, indices, energies, familyBias, accentBias, sampler = null) {
    const sorted           = [...indices].sort((a, b) => energies[b] - energies[a]);
    const familyWeightInfo = buildFamilyWeights(sorted, energies, familyBias);
    return function pickColor(salt = 'color') {
        const randomUnit = sampler ? () => sampler(salt) : Math.random;
        const idx        = pickWeightedIndex(sorted, familyWeightInfo, accentBias, randomUnit);
        return paletteColors[idx] ?? '#000000';
    };
}
