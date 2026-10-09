// Oracle: unchanged modules from ccawmiku/latent-art-wallpaper, recorded in reference/README.md.
import { DecoderNetwork } from './reference/decoder.js';
import { LatentEntity } from './reference/entity.js';
import { createStableSampler, createColorPicker } from './reference/colorPicker.js';
import { classicPalette } from './reference/palettes/classic.js';
import { linearRenderer } from './reference/renderers/linear.js';
let state = Number(process.argv[2]) >>> 0 || 1;
const seed = state;
Math.random = () => { state ^= state << 13; state ^= state >>> 17; state ^= state << 5; return (state >>> 0) / 4294967296; };
const decoder = new DecoderNetwork(), entity = new LatentEntity();
const params = entity.getPhysicalParams(decoder);
const vSampler = createStableSampler('vertical', params, 0, seed, 0.4);
const hSampler = createStableSampler('horizontal', params, 0, seed, 0.4);
const vPickColor = createColorPicker(classicPalette.colors, params.paletteIndices, params.energies, params.vPaletteBias, params.accentBias, s => vSampler(`palette:${s}`));
const hPickColor = createColorPicker(classicPalette.colors, params.paletteIndices, params.energies, params.hPaletteBias, params.accentBias, s => hSampler(`palette:${s}`));
const rgb565 = hex => { const c = Number.parseInt(hex.slice(1),16); return ((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&31); };
const pixels = new Uint16Array(240*240);
const context = {canvas:{width:240,height:240},clearRect(){},fillRect(x,y,w,h){
    const color = rgb565(this.fillStyle);
    for (let row=Math.max(0,y);row<Math.min(240,y+h);row++)
        pixels.fill(color,row*240+Math.max(0,x),row*240+Math.min(240,x+w));
}};
const parts = linearRenderer.render(context, {params,axisMode:'both',layerMode:'auto',vSampler,hSampler,vPickColor,hPickColor,paletteColors:classicPalette.colors}).parts;
let pixelHash = 2166136261;
for (const c of pixels) {
    pixelHash = Math.imul(pixelHash ^ (c>>8),16777619) >>> 0;
    pixelHash = Math.imul(pixelHash ^ (c&255),16777619) >>> 0;
}
console.log(JSON.stringify({pixel_hash:pixelHash,seed,raw:Array.from(params._forwardState.out),edge:params.edgeExpand,background:rgb565('#fafafa'),stripes:parts.map(p=>[p.pos,p.size,rgb565(p.color),p.dir==='vertical'?1:0])}));
