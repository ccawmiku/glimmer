import { LATENT_DIM } from './constants.js';
import { randNormal, sigmoid, tanh } from './math.js';

export class LatentEntity {
    constructor() {
        this.z        = new Float32Array(LATENT_DIM);
        this.velocity = new Float32Array(LATENT_DIM);
        for (let i = 0; i < LATENT_DIM; i++) {
            this.z[i]        = randNormal() * 1.2;
            this.velocity[i] = randNormal() * 0.01;
        }
    }

    step(baseSpeed = 0.04) {
        const pullToCenter = 0.012;
        const friction     = 0.88;
        for (let i = 0; i < LATENT_DIM; i++) {
            this.velocity[i] = this.velocity[i] * friction
                              + randNormal() * baseSpeed
                              - this.z[i] * pullToCenter * 0.01;
            this.z[i] += this.velocity[i];
            if (this.z[i] >  5.5) this.z[i] -= (this.z[i] - 5.5) * 0.08;
            if (this.z[i] < -5.5) this.z[i] -= (this.z[i] + 5.5) * 0.08;
        }
    }

    jump(amount = 0.9) {
        for (let i = 0; i < LATENT_DIM; i++) {
            this.z[i]        += randNormal() * amount;
            this.velocity[i]  = randNormal() * 0.02;
        }
    }

    /**
     * Decode physical render parameters from the latent vector.
     *
     * @param {import('./decoder.js').DecoderNetwork} decoder
     * @param {'relu'|'leaky'|'tanh'|'swish'} [activationMode='relu']
     * @param {{ colorLimit?: number, manualPaletteIndices?: number[] }} [opts]
     * @returns {object} params
     */
    getPhysicalParams(decoder, activationMode = 'relu', opts = {}) {
        const { colorLimit = 0, manualPaletteIndices = [] } = opts;
        const forwardState = decoder.forward(this.z, activationMode);
        const raw = forwardState.out;

        const colorThreshold     = raw[0] * 0.85;
        const colorTemperature   = 0.70 + sigmoid(raw[1]) * 1.50;
        const decodedColorTarget = 1 + Math.floor(sigmoid(raw[2]) * 31.999);
        const requestedColorCount = colorLimit > 0 ? colorLimit : decodedColorTarget;

        const stepMod        = 0.006 + sigmoid(raw[3])  * 0.014;
        const zSpread        = 0.15  + sigmoid(raw[4])  * 0.85;
        const globalZBias    = tanh(raw[5])  * 0.25;
        const accentBias     = sigmoid(raw[6]);
        const whitespaceBias = sigmoid(raw[7]) * 0.08;

        const vDensity     = 0.006 + sigmoid(raw[8])  * 0.11;
        const vWidthMean   = 0.45  + sigmoid(raw[9])  * 1.85;
        const vWidthJitter = 0.25  + sigmoid(raw[10]) * 1.75;
        const vSkipRate    = sigmoid(raw[11]) * 0.28;
        const vMinWidth    = 0.003 + sigmoid(raw[12]) * 0.014;
        const vClusterBias = sigmoid(raw[13]) * 2.2;
        const vZBias       = tanh(raw[14]) * 0.35;
        const vPaletteBias = tanh(raw[15]);

        const hDensity     = 0.006 + sigmoid(raw[16]) * 0.11;
        const hWidthMean   = 0.45  + sigmoid(raw[17]) * 1.85;
        const hWidthJitter = 0.25  + sigmoid(raw[18]) * 1.75;
        const hSkipRate    = sigmoid(raw[19]) * 0.28;
        const hMinWidth    = 0.003 + sigmoid(raw[20]) * 0.014;
        const hClusterBias = sigmoid(raw[21]) * 2.2;
        const hZBias       = tanh(raw[22]) * 0.35;
        const hPaletteBias = tanh(raw[23]);

        // Only a 1px expansion at draw time; no blurring.
        const edgeExpand      = Math.round(sigmoid(raw[24]) * 1.0);
        const familyBalance   = sigmoid(raw[25]);
        const randomTempo     = 0.035 + sigmoid(raw[26]) * 0.62;
        const randomPhaseBias = tanh(raw[27]) * 8.0;
        const randomPhaseWarp = sigmoid(raw[28]) * 22.0;

        // ── Palette energy selection ──────────────────────────────────
        const energies = [];
        let activeIndices = [];
        let maxEnergy = -Infinity;
        let maxIdx    = 0;

        for (let i = 0; i < 32; i++) {
            const energy = raw[32 + i] / colorTemperature;
            energies.push(energy);
            if (energy > maxEnergy) { maxEnergy = energy; maxIdx = i; }
            if (energy > colorThreshold) activeIndices.push(i);
        }

        if (colorLimit > 0) {
            activeIndices = energies
                .map((e, idx) => ({ idx, e }))
                .sort((a, b) => b.e - a.e)
                .slice(0, requestedColorCount)
                .map(x => x.idx);
        } else {
            if (activeIndices.length === 0) activeIndices.push(maxIdx);
            if (activeIndices.length > requestedColorCount) {
                activeIndices = activeIndices
                    .map(idx => ({ idx, e: energies[idx] }))
                    .sort((a, b) => b.e - a.e)
                    .slice(0, requestedColorCount)
                    .map(x => x.idx);
            }
        }

        if (manualPaletteIndices.length > 0) {
            activeIndices = manualPaletteIndices.slice();
        }

        return {
            paletteIndices: activeIndices, energies,
            colorThreshold, colorTemperature, activeColorCap: requestedColorCount,
            stepMod, zSpread, globalZBias, accentBias, whitespaceBias,
            vDensity, vWidthMean, vWidthJitter, vSkipRate, vMinWidth, vClusterBias, vZBias, vPaletteBias,
            hDensity, hWidthMean, hWidthJitter, hSkipRate, hMinWidth, hClusterBias, hZBias, hPaletteBias,
            edgeExpand, familyBalance, randomTempo, randomPhaseBias, randomPhaseWarp,
            _forwardState: forwardState,
        };
    }
}
