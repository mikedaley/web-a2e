// Fragment shader with comprehensive CRT effects
// Inspired by cool-retro-term (https://github.com/Swordfish90/cool-retro-term)

precision highp float;

uniform sampler2D u_texture;
uniform sampler2D u_burnInTexture;
uniform sampler2D u_selectionTexture;
uniform vec2 u_resolution;
uniform vec2 u_textureSize;
uniform float u_time;

// CRT effect uniforms
uniform float u_curvature;
uniform float u_scanlineIntensity;
uniform float u_scanlineWidth;
uniform float u_beamBloom;
// How hard the edge between two source dots is when the picture is magnified.
// 0 is plain bilinear, 1 puts the whole transition inside one output pixel.
uniform float u_sharpness;
uniform float u_shadowMask;
uniform float u_pixelRatio;
// Mask geometry: 0 = aperture grille (stripes), 1 = shadow mask (dot triad)
uniform int u_maskType;
uniform float u_glowIntensity;
uniform float u_glowSpread;
uniform float u_brightness;
uniform float u_contrast;
uniform float u_saturation;
uniform float u_vignette;
uniform float u_flicker;
uniform float u_rgbOffset;

// New effect uniforms
uniform float u_staticNoise;
uniform float u_jitter;
uniform float u_horizontalSync;
uniform float u_glowingLine;
uniform float u_ambientLight;
uniform float u_burnIn;
uniform float u_overscan;

// Color bleed - vertical inter-scanline blending (simulates CRT phosphor overlap)
uniform float u_colorBleed;

// Monochrome mode (0=color, 1=green, 2=amber, 3=white)
uniform int u_monochromeMode;

// Corner radius for rounded screen corners
uniform float u_cornerRadius;

// Beam position crosshair overlay (-1.0 = off, 0.0–1.0 = normalized position)
uniform float u_beamY;
uniform float u_beamX;

// Screen margin/padding for rounded corners (content is inset by this amount)
uniform float u_screenMargin;

// Screen inset — shrinks the screen to reveal bezel without curvature
uniform float u_screenInset;


// Background colour for pixels outside the curved screen area
uniform vec3 u_surroundColor;

varying vec2 v_texCoord;

// Constants
const float PI = 3.14159265359;

// ============================================
// Utility functions
// ============================================

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 3D -> 1D hash. The grain effect uses this with the frame counter as the third
// component so every frame is an independent noise field. Feeding the frame in
// as an offset added to the 2D coordinate instead (the old approach) only
// translates one fixed field, which reads as rows of grain sliding across the
// screen rather than as noise.
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float rgb2grey(vec3 v) {
    return dot(v, vec3(0.21, 0.72, 0.07));
}

// ============================================
// Screen overscan/border
// ============================================

vec2 applyOverscan(vec2 uv) {
    if (u_overscan < 0.001) return uv;

    // Scale UV coordinates inward to create a border
    // overscan of 1.0 = 10% border on each side (content fills 80% of screen)
    float borderSize = u_overscan * 0.1;
    float scale = 1.0 - (borderSize * 2.0);

    // Scale from center
    vec2 centered = uv - 0.5;
    vec2 scaled = centered / scale;
    return scaled + 0.5;
}

// ============================================
// Screen curvature (pincushion distortion)
// ============================================

vec2 curveUV(vec2 uv) {
    if (u_curvature < 0.001) return uv;

    vec2 cc = uv - 0.5;
    float dist = dot(cc, cc);
    float distortion = dist * u_curvature * 0.5;
    vec2 curved = uv + cc * distortion;

    return curved;
}

// ============================================
// Screen inset (shrink screen to reveal bezel)
// ============================================

vec2 applyScreenInset(vec2 uv) {
    if (u_screenInset < 0.001) return uv;

    vec2 centered = uv - 0.5;
    float scale = 1.0 + u_screenInset;
    return centered * scale + 0.5;
}

// ============================================
// Horizontal sync distortion
// ============================================

vec2 applyHorizontalSync(vec2 uv, float time) {
    if (u_horizontalSync < 0.001) return uv;

    float randVal = hash12(vec2(floor(time * 0.5), 0.0));
    if (randVal > u_horizontalSync) return uv;

    float distortionFreq = mix(4.0, 40.0, hash12(vec2(time * 0.1, 1.0)));
    float distortionScale = u_horizontalSync * 0.02 * randVal;
    float wave = sin((uv.y + time * 0.01) * distortionFreq);
    uv.x += wave * distortionScale;

    return uv;
}

// ============================================
// Jitter effect
// ============================================

vec2 applyJitter(vec2 uv, float time) {
    if (u_jitter < 0.001) return uv;

    vec2 noiseCoord = uv * 100.0 + vec2(time * 10.0, time * 7.0);
    vec2 offset = vec2(
        hash12(noiseCoord) - 0.5,
        hash12(noiseCoord + vec2(100.0, 0.0)) - 0.5
    );

    return uv + offset * u_jitter * 0.005;
}

// ============================================
// Static noise effect (TV static style)
// ============================================

float staticNoise(vec2 uv, float time) {
    if (u_staticNoise < 0.001) return 0.0;

    vec2 grain = floor(uv * u_textureSize);

    // Wrapped frame counter. Keeping the hash inputs small matters: the old
    // code added frame * 31 to the coordinate, so after a minute on screen the
    // inputs were in the tens of thousands and fract() had lost enough low bits
    // to make the field visibly repeat.
    float frame = mod(floor(time * 30.0), 1024.0);

    float noise = hash13(vec3(grain, frame));

    // Slight vignette on the noise
    vec2 cc = uv - 0.5;
    float dist = length(cc);
    float vignette = 1.0 - dist * 0.5;

    return (noise - 0.5) * u_staticNoise * vignette;
}

// ============================================
// Flicker effect
// ============================================

// Brightness undulation, as a set whose field rate is beating slowly against
// the mains shows it. Two things here are deliberate and must stay that way:
//
// The modulation is continuous and slow. It used to be a fresh random level
// held for 1/15s — up to fifteen discontinuous whole-screen luminance steps a
// second, right inside the 3-30Hz band that triggers photosensitive epilepsy.
// The fastest component below is 1.25Hz, so under a flash per second even
// counting each half cycle, well clear of the WCAG 2.3.1 limit of three.
//
// The amplitude is small. Peak to peak is 6% at the top of the slider, against
// the 10% relative luminance change that WCAG counts as a general flash. The
// old code reached 15%. Raising either of these puts the effect back over the
// line, and the flicker slider is on during normal use rather than only while
// the machine is off.
float flicker(float time) {
    if (u_flicker < 0.001) return 1.0;

    // Incommensurate periods (~0.78Hz and ~1.25Hz) so the beat never settles
    // into an obvious loop.
    float wobble = sin(time * 4.90) * 0.6 + sin(time * 7.85) * 0.4;

    return 1.0 + wobble * u_flicker * 0.03;
}

// ============================================
// Glowing line effect (scanning beam)
// ============================================

float glowingLine(vec2 uv, float time) {
    if (u_glowingLine < 0.001) return 0.0;

    float beamPos = fract(time * 0.05);
    float dist = abs(uv.y - beamPos);
    float glow = smoothstep(0.1, 0.0, dist);

    return glow * u_glowingLine * 0.3;
}

// ============================================
// Ambient light effect
// ============================================

vec3 applyAmbientLight(vec3 color, vec2 uv) {
    if (u_ambientLight < 0.001) return color;

    vec2 cc = uv - 0.5;
    float dist = length(cc);
    float ambient = (1.0 - dist) * (1.0 - dist);

    return color + vec3(u_ambientLight * ambient * 0.15);
}

// ============================================
// Scanline effect
// ============================================

// The beam is a Gaussian spot, and its diameter grows with beam current. A
// bright line is therefore physically fatter than a dark one and fills more of
// the gap to its neighbours, which is why white text on a CRT looks bolder than
// the same glyphs in a screenshot and why dark areas keep a crisp line
// structure that bright areas lose. The old fixed comb — a sin() raised to a
// constant power — could not express that: it darkened every line by the same
// fraction no matter what was on it, which reads as stripes laid over the
// picture rather than as a raster.
//
// `luma` is the luminance actually being displayed at this fragment, so the
// width responds to the final image (bleed, fringing and selection overlay
// included) rather than to a raw texture sample.
// ============================================
// Magnification
// ============================================

// Sharp bilinear: interpolate only across the seam between two source dots,
// and keep each dot's interior flat.
//
// A 560 wide picture magnified past 1400 output pixels gives every dot a two
// to five pixel ramp under plain bilinear, and that is horizontal blur laid on
// top of a signal the core has already band limited. Plain nearest is the
// other extreme: hard dots, but they alias and crawl once curvature or jitter
// move the sampling grid. This sits between the two. The seam gets a ramp
// about one output pixel wide, which is enough to stay alias free, and the
// rest of the dot is left alone.
//
// The width of that ramp is worked out from the uniforms rather than from
// fwidth(), because derivatives are an extension in GLSL ES 1.00 and this
// shader has to run on WebGL 1 as well.
vec2 sharpenUV(vec2 uv) {
    if (u_sharpness < 0.001) return uv;

    vec2 px = uv * u_textureSize;
    vec2 seam = floor(px + 0.5);
    vec2 offset = px - seam;

    // Source texels per output pixel. Clamped to 1 so a picture being shrunk
    // is never *softened* by this: below one texel per pixel there is no
    // magnification seam to sharpen.
    vec2 span = min(u_textureSize / max(u_resolution, vec2(1.0)), vec2(1.0));
    span = max(span, vec2(1e-4));

    vec2 hard = clamp(offset / span, -0.5, 0.5);
    return (seam + mix(offset, hard, u_sharpness)) / u_textureSize;
}

float scanlines(vec2 uv, float luma) {
    if (u_scanlineIntensity < 0.001) return 1.0;

    // The framebuffer is 560x384 — the Apple's 280x192 doubled — so a scanline
    // pitch is two texel rows. This yields 192 lines, matching the real raster.
    float linePos = uv.y * u_textureSize.y * 0.5;

    // Distance from the centre of the nearest line, in pitch units.
    float dist = fract(linePos) - 0.5;

    // Spot size. The base is the user's scanline width; brightness widens it
    // from there. sqrt() because perceived brightness runs ahead of beam
    // current — mid tones should already be blooming, not just peak white.
    float sigma = mix(0.18, 0.42, u_scanlineWidth);
    sigma *= 1.0 + sqrt(clamp(luma, 0.0, 1.0)) * u_beamBloom * 1.6;

    // Gaussian profile, peak 1.0 at the line centre. Never exceeding 1.0 means
    // a bloomed line loses less to the gaps rather than being boosted above its
    // own colour, so the effect cannot brighten the picture beyond the source.
    float x = dist / sigma;
    float profile = exp(-0.5 * x * x);

    return mix(1.0, profile, u_scanlineIntensity);
}

// ============================================
// Shadow mask
// ============================================

// The mask is a physical object: a perforated sheet a few millimetres behind
// the glass, with a pitch fixed in millimetres. Two things follow, and both
// were wrong before.
//
// It does not change size. The old code took its pitch from device pixels
// (mod(pos.x, 3.0) on a coordinate scaled by u_resolution), so on a Retina
// display the whole triad spanned three physical pixels and was effectively
// invisible — which is why the slider appeared to do so much less than it
// should — and the pattern resized whenever the window moved between monitors
// of different density. Working in CSS pixels via u_pixelRatio pins the pitch
// to a constant apparent size wherever the page is viewed.
//
// It does not move with the signal. The old call passed the beam-distorted UV,
// so jitter and horizontal sync dragged the mask around with the picture. A
// mask is glued to the tube; the raster slides behind it. Deriving position
// from gl_FragCoord ties it to the physical screen and nothing else.
//
// MASK_PITCH is one full RGB triad. Three CSS pixels puts one phosphor stripe
// per CSS pixel, fine enough to read as texture rather than as stripes.
const float MASK_PITCH = 3.0;

vec3 shadowMask() {
    if (u_shadowMask < 0.001) return vec3(1.0);

    // Position on the glass, in CSS pixels.
    vec2 pos = gl_FragCoord.xy / max(u_pixelRatio, 0.001);

    vec3 mask;

    if (u_maskType == 1) {
        // Dot triad, as the Apple Monitor //e and most consumer sets used.
        // Triads sit on a staggered lattice — every other row is offset by half
        // a triad — which is what stops a shadow mask reading as vertical
        // stripes the way an aperture grille does.
        float row = floor(pos.y / MASK_PITCH);
        float stagger = mod(row, 2.0) * 0.5;
        float idx = mod(floor(pos.x / (MASK_PITCH / 3.0) + stagger * 1.5), 3.0);

        mask = vec3(0.7);
        if (idx < 0.5) mask.r = 1.0;
        else if (idx < 1.5) mask.g = 1.0;
        else mask.b = 1.0;

        // Gap between mask rows. Without it the triads merge vertically into
        // continuous stripes and the stagger buys nothing.
        float withinRow = fract(pos.y / MASK_PITCH);
        float gap = smoothstep(0.0, 0.25, withinRow) * smoothstep(1.0, 0.75, withinRow);
        mask *= mix(0.8, 1.0, gap);
    } else {
        // Aperture grille: continuous vertical phosphor stripes, no horizontal
        // structure. A Trinitron look rather than an Apple one, but it is what
        // this shader has always drawn, so it stays the default.
        float idx = mod(floor(pos.x / (MASK_PITCH / 3.0)), 3.0);

        mask = vec3(0.7);
        if (idx < 0.5) mask.r = 1.0;
        else if (idx < 1.5) mask.g = 1.0;
        else mask.b = 1.0;
    }

    return mix(vec3(1.0), mask, u_shadowMask);
}

// ============================================
// Vignette effect
// ============================================

float vignette(vec2 uv) {
    if (u_vignette < 0.001) return 1.0;

    vec2 center = uv - 0.5;
    float dist = length(center);
    float vig = 1.0 - dist * dist * u_vignette * 2.0;
    return clamp(vig, 0.0, 1.0);
}

// ============================================
// Phosphor glow / bloom effect
// ============================================

vec3 glow(sampler2D tex, vec2 uv) {
    if (u_glowIntensity < 0.001) return vec3(0.0);

    vec3 bloom = vec3(0.0);
    float spread = u_glowSpread * 0.01;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * spread;
            bloom += texture2D(tex, uv + offset).rgb;
        }
    }
    bloom /= 9.0;

    return bloom * u_glowIntensity;
}

// ============================================
// RGB chromatic aberration
// ============================================

// Misconvergence: the three beams land on exactly the same spot only where the
// convergence assembly was adjusted to put them, which is the centre. The error
// grows with deflection angle, so a real tube is clean in the middle and worst
// in the corners — the reason service manuals specify convergence at centre,
// edge and corner separately.
//
// `screenUV` is the position on the glass, not the beam-distorted coordinate:
// convergence error is a property of the deflection geometry, so it belongs to
// where you are looking, not to a picture that jitter has slid sideways.
vec3 rgbShift(sampler2D tex, vec2 uv, vec2 screenUV) {
    if (u_rgbOffset < 0.001) return texture2D(tex, uv).rgb;

    vec2 dir = screenUV - 0.5;

    // Quadratic in radius. The old code scaled linearly, which spreads the
    // error evenly out from the centre; real misconvergence stays negligible
    // across the middle of the screen and then climbs sharply, so the corners
    // are where it shows. Squaring keeps the corner magnitude while cleaning up
    // the centre two thirds.
    float r2 = dot(dir, dir);
    float amount = u_rgbOffset * 0.0045 * r2;

    // Horizontal deflection covers a wider angle than vertical on a 4:3 tube,
    // so the error is not isotropic — it is consistently worse left-to-right.
    vec2 anisotropy = vec2(1.0, 0.75);

    // Red outward, blue inward, green a small share the other way. Green is
    // usually the reference gun in a delta arrangement and moves least, but it
    // does move: leaving it exactly still reads as two colours fringing rather
    // than three beams missing each other.
    vec2 rOffset = dir * amount * anisotropy;
    vec2 bOffset = -dir * amount * anisotropy;
    vec2 gOffset = dir * amount * anisotropy * -0.15;

    float r = texture2D(tex, uv + rOffset).r;
    float g = texture2D(tex, uv + gOffset).g;
    float b = texture2D(tex, uv + bOffset).b;

    return vec3(r, g, b);
}

// ============================================
// Color Bleed (vertical inter-scanline blending)
// Simulates CRT phosphor spot overlap where
// adjacent scanlines bleed into each other
// ============================================

vec3 colorBleed(sampler2D tex, vec2 uv, vec3 baseColor) {
    if (u_colorBleed < 0.001) return baseColor;

    vec2 texelSize = 1.0 / u_textureSize;

    // 5-tap vertical kernel: sample 2 rows above and below
    // Weights 1-1-2-1-1 (sum 6) chosen to perfectly cancel the common
    // Apple II HIRES pattern where artifact colors alternate every 2 rows
    // (each scanline is doubled, giving a BBVV period-4 pattern)
    vec3 up2 = texture2D(tex, uv + vec2(0.0, -2.0 * texelSize.y)).rgb;
    vec3 up1 = texture2D(tex, uv + vec2(0.0, -1.0 * texelSize.y)).rgb;
    vec3 dn1 = texture2D(tex, uv + vec2(0.0,  1.0 * texelSize.y)).rgb;
    vec3 dn2 = texture2D(tex, uv + vec2(0.0,  2.0 * texelSize.y)).rgb;

    vec3 blended = (up2 + up1 + baseColor * 2.0 + dn1 + dn2) / 6.0;

    return mix(baseColor, blended, u_colorBleed);
}

// ============================================
// Color adjustment
// ============================================

vec3 adjustColor(vec3 color) {
    color *= u_brightness;
    color = (color - 0.5) * u_contrast + 0.5;
    float gray = rgb2grey(color);
    color = mix(vec3(gray), color, u_saturation);
    return color;
}

// ============================================
// Monochrome mode
// ============================================

vec3 applyMonochrome(vec3 color) {
    if (u_monochromeMode == 0) return color; // Color mode - no change

    // Convert to grayscale using luminance
    float gray = rgb2grey(color);

    // Apply tint based on monochrome mode
    if (u_monochromeMode == 1) {
        // Green phosphor (classic Apple II monitor)
        // P1 phosphor green: slightly blue-green tint
        return vec3(gray * 0.2, gray * 1.0, gray * 0.2);
    } else if (u_monochromeMode == 2) {
        // Amber phosphor (common on IBM PCs)
        // Warm orange-yellow tint
        return vec3(gray * 1.0, gray * 0.75, gray * 0.2);
    } else if (u_monochromeMode == 3) {
        // White phosphor (paper white)
        // Slight warm tint for authenticity
        return vec3(gray * 1.0, gray * 1.0, gray * 0.9);
    }

    return color;
}

// ============================================
// Edge effects
// ============================================

float edgeFade(vec2 uv) {
    vec2 edge = smoothstep(0.0, 0.005, uv) * smoothstep(0.0, 0.005, 1.0 - uv);
    return mix(0.85, 1.0, edge.x * edge.y);
}

float smoothEdge(vec2 uv) {
    if (u_cornerRadius < 0.001) return 1.0;

    float aspect = u_textureSize.x / u_textureSize.y;
    vec2 centered = uv - 0.5;
    float ry = u_cornerRadius;
    float rx = ry / aspect;
    vec2 cornerDist = abs(centered) - (0.5 - vec2(rx, ry));
    cornerDist = max(cornerDist, 0.0);
    vec2 screenDist = cornerDist * vec2(aspect, 1.0);
    float corner = length(screenDist) / ry;

    return 1.0 - smoothstep(0.9, 1.0, corner);
}

// Rounded rectangle SDF for clean corner masking
float roundedRectAlpha(vec2 uv, float radius) {
    if (radius < 0.001) return 1.0;

    vec2 centered = abs(uv - 0.5);
    vec2 cornerDist = centered - (0.5 - radius);

    // Inside the rectangle (not in corner region)
    if (cornerDist.x < 0.0 || cornerDist.y < 0.0) {
        return 1.0;
    }

    // In corner region - use distance from corner arc
    float dist = length(cornerDist);
    // Smooth anti-aliased edge
    return 1.0 - smoothstep(radius - 0.005, radius + 0.005, dist);
}

// Apply screen margin - scales content inward so corners don't clip it
vec2 applyScreenMargin(vec2 uv) {
    if (u_screenMargin < 0.001) return uv;

    // Scale UV from center to create margin
    vec2 centered = uv - 0.5;
    float scale = 1.0 / (1.0 - u_screenMargin * 2.0);
    return centered * scale + 0.5;
}

// ============================================
// Bezel shading (inner TV surround)
// ============================================

vec3 bezelShade(vec2 uv, vec2 curvedUV) {
    vec3 bezel = u_surroundColor;

    // Distance from screen centre (0 at centre, ~0.7 at corners)
    vec2 centered = uv - 0.5;
    float dist = length(centered);

    // 1. Inner shadow — darken where bezel meets the glass edge
    //    Uses distance from the [0,1] rect boundary
    vec2 edgeDist = min(uv, 1.0 - uv);           // 0 at edge, 0.5 at centre
    float innerShadow = smoothstep(0.0, 0.12, min(edgeDist.x, edgeDist.y));
    bezel *= mix(0.45, 1.0, innerShadow);

    // 2. Corner vignette — additional darkening in corners
    float cornerDark = 1.0 - dist * dist * 0.6;
    bezel *= clamp(cornerDark, 0.5, 1.0);

    // 3. Subtle warm-to-cool color shift toward edges (simulates age/wear)
    float edgeFactor = smoothstep(0.2, 0.7, dist);
    bezel = mix(bezel, bezel * vec3(0.92, 0.90, 0.88), edgeFactor * 0.5);

    // 4. Fine grain noise — breaks up flat color for a matte plastic feel
    vec2 grainCoord = uv * u_resolution * 0.5;
    float grain = hash12(grainCoord + vec2(floor(u_time * 0.5))) * 2.0 - 1.0;
    bezel += grain * 0.015;

    // 5. Thin highlight line at the inner lip (glass-to-bezel ridge)
    float lipDist = min(edgeDist.x, edgeDist.y);
    float lip = smoothstep(0.008, 0.004, lipDist) * smoothstep(0.0, 0.002, lipDist);
    bezel += vec3(lip * 0.2);

    // There is deliberately no screen reflection on the bezel.
    //
    // The surround is matte plastic. A diffuse surface scatters light rather
    // than forming an image, so it cannot show a reflection of what is on
    // screen — no amount of sampling the framebuffer produces something a real
    // monitor does. What a real bezel does pick up is plain illumination: near
    // a bright screen it lightens and takes on the average colour of whatever
    // is next to it, with no detail whatsoever.
    //
    // That illumination was modelled here and removed: on anything but a
    // near-white screen it is a barely perceptible lightening of the innermost
    // few millimetres, and it cost a 13-tap gather of the screen texture on
    // every bezel fragment to produce it. If it comes back it should be a cheap
    // approximation of the average edge colour, not a per-fragment gather, and
    // it should be described as light spill rather than reflection.

    return clamp(bezel, 0.0, 1.0);
}

// ============================================
// Beam position crosshair overlay
// ============================================

vec3 beamOverlay(vec2 uv) {
    if (u_beamY < 0.0 && u_beamX < 0.0) return vec3(0.0);

    // Line thickness in UV space (~1.5 pixels)
    float lineW = 1.5 / u_textureSize.x;
    float lineH = 1.5 / u_textureSize.y;

    vec3 lineColor = vec3(1.0, 0.0, 0.0); // Red
    float intensity = 0.0;

    // Horizontal line at beamY
    if (u_beamY >= 0.0 && u_beamY <= 1.0) {
        float dy = abs(uv.y - u_beamY);
        intensity += smoothstep(lineH, 0.0, dy) * 0.6;
    }

    // Vertical line at beamX
    if (u_beamX >= 0.0 && u_beamX <= 1.0) {
        float dx = abs(uv.x - u_beamX);
        intensity += smoothstep(lineW, 0.0, dx) * 0.6;
    }

    return lineColor * min(intensity, 1.0);
}

// ============================================
// Main fragment shader
// ============================================

void main() {
    vec2 uv = v_texCoord;

    // Stable screen boundary from undistorted coordinates.
    // The physical CRT mask doesn't wobble — only the beam does.
    // All clipping and alpha use this so nothing renders outside the edge.
    vec2 stableCurvedUV = applyScreenInset(curveUV(uv));

    // Compute bezel color once (with shading effects applied)
    vec3 bezel = bezelShade(uv, stableCurvedUV);

    float cornerAlpha = roundedRectAlpha(stableCurvedUV, u_cornerRadius);
    if (cornerAlpha < 0.001) {
        gl_FragColor = vec4(bezel, 1.0);
        return;
    }

    float edgeFactor = smoothEdge(stableCurvedUV);
    if (edgeFactor < 0.001) {
        gl_FragColor = vec4(bezel, 1.0);
        return;
    }

    if (stableCurvedUV.x < 0.0 || stableCurvedUV.x > 1.0 || stableCurvedUV.y < 0.0 || stableCurvedUV.y > 1.0) {
        gl_FragColor = vec4(bezel, 1.0);
        return;
    }

    // Apply signal distortions — the beam wobbles, the mask does not
    vec2 distortedUV = applyHorizontalSync(uv, u_time);
    distortedUV = applyJitter(distortedUV, u_time);
    vec2 curvedUV = applyScreenInset(curveUV(distortedUV));

    // Content coordinates use the distorted beam position
    vec2 contentUV = applyOverscan(curvedUV);
    contentUV = applyScreenMargin(contentUV);
    // Harden the seams between source dots before anything samples them, so
    // the rgb shift and the colour bleed read the same picture the scanlines
    // and the mask will be laid over.
    contentUV = sharpenUV(contentUV);

    // Dark bezel color for areas outside content
    vec3 darkBezelColor = vec3(0.0); // Black

    // Check if we're in the margin area (outside content but inside screen)
    bool inMargin = contentUV.x < 0.0 || contentUV.x > 1.0 || contentUV.y < 0.0 || contentUV.y > 1.0;

    // Get base color - dark bezel color for margin area, texture sample for content
    vec3 color;
    if (inMargin) {
        color = darkBezelColor;
    } else {
        // Get base color with RGB shift
        color = rgbShift(u_texture, contentUV, stableCurvedUV);

        // Apply vertical color bleed (CRT inter-scanline blending)
        color = colorBleed(u_texture, contentUV, color);
    }

    // Apply texture-based effects only for content area
    if (!inMargin) {
        // Blend text selection overlay (before burn-in and glow so CRT effects apply on top)
        vec4 sel = texture2D(u_selectionTexture, contentUV);
        if (sel.a > 0.0) {
            color = mix(color, sel.rgb, sel.a);
        }

        // Apply burn-in from accumulation buffer
        if (u_burnIn > 0.001) {
            // Burn-in texture is stored in non-flipped coords, flip Y to match main texture
            vec2 burnInCoord = vec2(contentUV.x, 1.0 - contentUV.y);
            vec3 burnInColor = texture2D(u_burnInTexture, burnInCoord).rgb;
            color = max(color, burnInColor * u_burnIn);
        }

        // Add phosphor glow
        color += glow(u_texture, contentUV);
    }

    // Apply scanlines (use curvedUV for consistent scanlines across margin)
    color *= scanlines(curvedUV, rgb2grey(color));

    // Apply shadow mask
    color *= shadowMask();

    // Apply color adjustments (brightness, contrast, saturation)
    color = adjustColor(color);

    // Apply monochrome mode (after color adjustments, before vignette)
    color = applyMonochrome(color);

    // Apply vignette
    color *= vignette(curvedUV);

    // Apply edge fade for curved screens (uses stable coords — physical screen property)
    if (u_curvature > 0.001) {
        color *= edgeFade(stableCurvedUV);
    }

    // Apply flicker
    color *= flicker(u_time);

    // Add glowing line
    color += vec3(glowingLine(curvedUV, u_time));

    // Add static noise
    color += vec3(staticNoise(curvedUV, u_time));

    // Apply ambient light
    color = applyAmbientLight(color, curvedUV);

    // Beam position crosshair (opaque overlay, stable UV — only curve applied)
    {
        vec2 beamUV = applyOverscan(stableCurvedUV);
        beamUV = applyScreenMargin(beamUV);
        vec3 beam = beamOverlay(beamUV);
        color = mix(color, vec3(1.0, 0.0, 0.0), beam.r);
    }

    // Clamp final color
    color = clamp(color, 0.0, 1.0);

    // Blend screen content with bezel at curved edges
    float alpha = cornerAlpha * edgeFactor;
    color = mix(bezel, color, alpha);

    gl_FragColor = vec4(color, 1.0);
}
