/**
 * Shared part-sorting and layer-label helpers used by stripe-style renderers
 * (linear, radial, pixel grid, …).
 */

/** Sort parts array in-place according to layerMode. */
export function sortPartsForLayerMode(parts, layerMode) {
    if (layerMode === 'xTop') {
        parts.sort((a, b) => {
            const ax = a.dir === 'vertical'   || a.dir === 'sector' ? 1 : 0;
            const bx = b.dir === 'vertical'   || b.dir === 'sector' ? 1 : 0;
            return ax - bx || a.z - b.z;
        });
    } else if (layerMode === 'yTop') {
        parts.sort((a, b) => {
            const ay = a.dir === 'horizontal' || a.dir === 'ring' ? 1 : 0;
            const by = b.dir === 'horizontal' || b.dir === 'ring' ? 1 : 0;
            return ay - by || a.z - b.z;
        });
    } else {
        parts.sort((a, b) => a.z - b.z);
    }
}

/** Human-readable layer label used in the param overlay. */
export function getLayerLabel(layerMode, isRadial = false) {
    if (layerMode === 'xTop') return isRadial ? 'SECTOR TOP' : 'VERT TOP';
    if (layerMode === 'yTop') return isRadial ? 'RING TOP'   : 'HORIZ TOP';
    return 'AUTO';
}
