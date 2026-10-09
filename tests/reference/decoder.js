import { LATENT_DIM, HIDDEN_1, HIDDEN_2, OUT_DIM } from './constants.js';
import { randUniform, relu, tanh, leakyRelu, swish } from './math.js';

// ── Weight initialisation ─────────────────────────────────────────────────

function initWeight(length, fanIn, fanOut, gain = 1) {
    const arr   = new Float32Array(length);
    const limit = gain * Math.sqrt(6 / (fanIn + fanOut));
    for (let i = 0; i < arr.length; i++) arr[i] = randUniform(limit);
    return arr;
}

// ── Activation helpers ────────────────────────────────────────────────────

/** @param {'relu'|'leaky'|'tanh'|'swish'} mode */
export function getActivation(mode) {
    if (mode === 'leaky') return leakyRelu;
    if (mode === 'tanh')  return tanh;
    if (mode === 'swish') return swish;
    return relu;
}

/** @param {'relu'|'leaky'|'tanh'|'swish'} mode */
export function getActivationLabel(mode) {
    if (mode === 'leaky') return 'LEAKY';
    if (mode === 'tanh')  return 'TANH';
    if (mode === 'swish') return 'SWISH';
    return 'RELU';
}

// ── Decoder network ───────────────────────────────────────────────────────

export class DecoderNetwork {
    constructor() {
        this.w1 = initWeight(LATENT_DIM * HIDDEN_1, LATENT_DIM, HIDDEN_1, 1.15);
        this.b1 = new Float32Array(HIDDEN_1);

        this.w2 = initWeight(HIDDEN_1 * HIDDEN_2, HIDDEN_1, HIDDEN_2, 1.20);
        this.b2 = new Float32Array(HIDDEN_2);

        this.w3 = initWeight(HIDDEN_2 * OUT_DIM, HIDDEN_2, OUT_DIM, 1.10);
        this.b3 = new Float32Array(OUT_DIM);

        for (let i = 0; i < OUT_DIM; i++) this.b3[i] = randUniform(0.2);
    }

    dense(input, inDim, outDim, weights, bias, activation) {
        const out = new Float32Array(outDim);
        for (let i = 0; i < outDim; i++) {
            let sum = bias[i];
            const offset = i * inDim;
            for (let j = 0; j < inDim; j++) sum += input[j] * weights[offset + j];
            out[i] = activation(sum);
        }
        return out;
    }

    /**
     * Full forward pass — returns intermediate activations for visualisation.
     * @param {Float32Array} latentZ
     * @param {'relu'|'leaky'|'tanh'|'swish'} [activationMode='relu']
     * @returns {{ latent: Float32Array, h1: Float32Array, h2: Float32Array, out: Float32Array }}
     */
    forward(latentZ, activationMode = 'relu') {
        const h1  = this.dense(latentZ, LATENT_DIM, HIDDEN_1, this.w1, this.b1, x => tanh(x));
        const h2  = this.dense(h1,      HIDDEN_1,   HIDDEN_2, this.w2, this.b2, getActivation(activationMode));
        const out = this.dense(h2,      HIDDEN_2,   OUT_DIM,  this.w3, this.b3, x => x);
        return { latent: latentZ, h1, h2, out };
    }

    /** Convenience: returns only the output vector. */
    decode(latentZ, activationMode = 'relu') {
        return this.forward(latentZ, activationMode).out;
    }
}
