/**
 * Built-in palette: Classic 32-colour set (original FULL_PALETTE).
 * All palettes must have exactly 32 colours (indices 0-31 match decoder energy slots).
 */
import { registerPalette } from './registry.js';

export const classicPalette = {
    label: 'CLASSIC',
    colors: [
        '#F8F9FA', '#FDF5E6', '#F0E68C', '#FFD700',
        '#DAA520', '#FF8C00', '#FF4500', '#E32636',
        '#B22222', '#A0522D', '#8B4513', '#6B4423',
        '#C71585', '#800080', '#4B0082', '#120A8F',
        '#003399', '#0F52BA', '#48CAE4', '#008080',
        '#50C878', '#00FA9A', '#228B22', '#808000',
        '#556B2F', '#D2B48C', '#BC8F8F', '#708090',
        '#778899', '#A9A9A9', '#2F4F4F', '#191919',
    ],
};

registerPalette('classic', classicPalette);
