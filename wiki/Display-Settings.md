# Display Settings

The Display Settings window controls every visual effect applied to the emulator screen. Open it from **View > Display**.

The window leads with a **Monitor** preset, which sets the whole picture in one choice. Below that are the image adjustments you are most likely to touch, and everything else folds away behind **Advanced**. **Reset to Defaults** at the bottom returns everything to its initial state.

**Display settings are remembered per machine.** A //e's soft composite look, which is exactly right for games, has no business on a IIgs's RGB desktop — so each machine has its own `localStorage` key and its own settings, and switching machines brings that machine's picture back rather than carrying the last one across. The key from before there was more than one machine is read once as the //e's.

Each machine's defaults differ in exactly one value, the **Screen Border**: 35% on the 8-bit machines, whose picture fills the frame, and 0 on a IIgs, which draws a border of its own as part of the raster.

Saved display profiles stay **global** — they are named snapshots any machine may pick.

---

## Table of Contents

- [Monitor Presets](#monitor-presets)
- [Image Controls](#image-controls)
- [CRT Effects](#crt-effects)
- [Analog Effects](#analog-effects)
- [Bezel](#bezel)
- [Rendering Options](#rendering-options)
- [NTSC Effects](#ntsc-effects)
- [Accessibility](#accessibility)
- [The No-Signal Screen](#the-no-signal-screen)
- [How the Shader Pipeline Works](#how-the-shader-pipeline-works)

---

## Monitor Presets

Each preset names something a real //e was plugged into, and its values follow from that hardware rather than from taste.

| Preset | What it imitates |
|--------|------------------|
| **Pixel Exact** | No CRT simulation at all -- sharp square pixels, nearest-neighbour filtering. The default. |
| **Composite Color** | A colour television or composite monitor: dot triad mask, NTSC fringing, heavy chroma bleed, softened image. |
| **RGB Monitor** | Separate colour signals: sharp, aperture grille, no fringing and almost no bleed, because there is no encoded signal to decode. |
| **Monochrome Green** | A P1 phosphor tube: long persistence, generous glow, and **no mask** -- a shadow mask exists only to keep three beams apart, and a monochrome tube has one. |
| **Monochrome Amber** | The warmer P3 tube, otherwise as above. |

Two behaviours are worth knowing:

- **Presets do not touch your calibration.** Brightness, contrast, saturation and the bezel settings are yours; switching monitor leaves them exactly as you set them.
- **Editing a setting a preset owns relabels it "Custom".** Nothing is reset when this happens -- only the label changes, because a preset name that no longer describes what is on screen is worse than no name. Editing a setting the preset does *not* own (brightness, bezel) leaves the preset name intact.

Selecting **Custom** explicitly does nothing: it is the label for hand-tuned settings, so choosing it must not throw that work away.

## Image Controls

Always visible, because these are calibration rather than simulation.

| Setting | Default | Description |
|---------|---------|-------------|
| **Brightness** | 100% | Overall luminance. Maps to a 0.5-1.5 multiplier in the shader. |
| **Contrast** | 100% | Difference between dark and light areas. Also 0.5-1.5. |
| **Saturation** | 100% | Colour intensity. 0% is greyscale, 200% is vivid. |

---

Everything below lives under **Advanced**.

## CRT Effects

| Setting | Description |
|---------|-------------|
| **Screen Curvature** | Barrel distortion bowing the image outward, mimicking curved glass. 0% is perfectly flat. |
| **Screen Border** | Overscan -- a dark border around the display content, as monitor bezels masked the raster edges. |
| **Scanlines** | Visible gaps between phosphor rows. The framebuffer is 560x384 (280x192 doubled), so a scanline pitch is two texel rows, giving the real 192 lines. |
| **Beam Bloom** | How much a bright scanline widens relative to a dark one. A CRT's beam spot grows with beam current, so bright lines are physically fatter and fill more of the gap to their neighbours -- it is why white text on a CRT looks bolder than the same glyphs in a screenshot. Only visible through the scanline comb, so it ships on (60%) rather than at zero. |
| **Shadow Mask** | Strength of the phosphor mask pattern. See **Mask Type** under Rendering for its geometry. The pattern keeps a fixed apparent size regardless of display density, and stays fixed to the glass rather than moving with jitter or sync distortion. |
| **Phosphor Glow** | Bloom around bright pixels, as phosphor bleeds light outward. |
| **Vignette** | Darkens corners and edges, reproducing brightness falloff at the periphery. |
| **RGB Offset** | Chromatic aberration -- shifts the colour channels apart, simulating convergence error in the electron guns. |
| **Flicker** | Slow brightness undulation, as on a set whose field rate beats against the mains. Deliberately gentle -- see [Accessibility](#accessibility). |

## Analog Effects

Signal-path imperfections and environmental characteristics.

| Setting | Description |
|---------|-------------|
| **Static Noise** | Grain over the picture, as from a slightly noisy signal. |
| **Jitter** | Random per-pixel displacement, simulating timing instability. |
| **Horizontal Sync** | Occasional horizontal distortion, mimicking sync problems in composite video. |
| **Glowing Line** | A bright band sweeping slowly down the screen (one pass every 20 seconds). |
| **Ambient Light** | Room light reflecting off the glass. |
| **Burn In** | Phosphor persistence -- bright areas leave a fading afterimage, accumulated in a separate framebuffer. Both monochrome presets set this high. |

## Bezel

| Setting | Description |
|---------|-------------|
| **Bezel Width** | Insets the screen to reveal the surrounding bezel without needing curvature. |
| **Bezel Color** | Colour picker for the surround. |

The bezel does not reflect the screen. It is matte plastic, and a diffuse surface scatters light rather than forming an image, so there is no reflection of screen content to show — an earlier Spill Reach / Spill Intensity pair attempted it and was removed. The surround's appearance is a static property of the moulding and does not change with what is on screen.

## Rendering Options

| Setting | Description |
|---------|-------------|
| **Mask Type** | **Aperture Grille** (continuous vertical stripes -- a Trinitron look) or **Shadow Mask** (dot triads on a staggered lattice, as an Apple Monitor //e actually used). The stagger is what stops a shadow mask reading as vertical stripes. |
| **Display Mode** | **Color**, **Green**, **Amber** or **White**. Monochrome modes bypass NTSC colour artefacts and tint a single-channel image. |
| **Sharp Pixels** | Nearest-neighbour texture filtering instead of bilinear. On by default, matching the Pixel Exact preset. |

## NTSC Effects

| Setting | Description |
|---------|-------------|
| **Color Bleed** | Vertical inter-scanline colour blending, as phosphor rows overlap. |
| **NTSC Fringing** | Magenta/cyan fringes at sharp horizontal edges, from chroma bandwidth limiting in the composite signal. |

## Accessibility

Animated effects are bounded by the limits for flashing content: no more than three flashes per second, and under a 10% change in screen luminance (WCAG 2.3.1).

Two effects previously exceeded both and were changed:

- **Flicker** held a fresh random brightness level for a fifteenth of a second -- up to fifteen whole-screen luminance steps a second, inside the 3-30 Hz band that triggers photosensitive seizures. It is now two slow incommensurate sines (~0.78 Hz and ~1.25 Hz) at a third of the amplitude, which is also closer to what a real set does.
- **Full-screen television static**, shown while the machine was off, rebuilt a high-contrast noise field at 50 Hz with a 12 Hz brightness modulation on top. It was removed outright and replaced with the no-signal message below.

If you extend the shader, keep new animated effects inside those limits. The constraints are documented in the shader functions themselves.

## The No-Signal Screen

With the machine powered off the screen shows **NO SIGNAL** and a line telling you to switch it on. It is built as an ordinary 560x384 framebuffer (`src/js/display/no-signal-frame.js`) and uploaded as the source texture, so it passes through the entire CRT chain exactly as emulator video does -- picking up whatever curvature, scanlines, mask, colour mode and phosphor settings you have chosen.

It is also more accurate than snow: snow is a *tuner* artefact, and a //e drives a monitor with no tuner. Pull the signal from one of those and you get a black screen.

There is deliberately no power symbol drawn on it. The emulator's canvas ignores clicks, so an icon there only invites people to press the one thing on screen that cannot work, instead of the power button in the toolbar.

## How the Shader Pipeline Works

1. **Source texture** -- the C++ video renderer produces a 560x384 RGBA framebuffer (the native 280x192 doubled), uploaded to a WebGL texture each frame. When `SharedArrayBuffer` is available the Worker writes it straight into shared memory; see [[Worker-Architecture]].

2. **CRT fragment shader** (`public/shaders/crt.glsl`) -- applies curvature, scanlines with the beam-bloom profile, shadow mask, phosphor glow, vignette, chromatic aberration, flicker, static noise, jitter, horizontal sync, glowing line, ambient light, colour bleed, NTSC fringing, monochrome tinting, overscan and rounded corners in a single pass.

3. **Burn-in pass** (`public/shaders/burnin.glsl`) -- a separate framebuffer accumulates bright pixels and decays them, blended back during the CRT pass.

4. **Edge overlay pass** (`public/shaders/edge.glsl`) -- a subtle highlight around the screen border, simulating light catching the edge of the glass.

Two coordinate spaces matter in the shader, and mixing them up is the usual source of bugs. Effects that model the **signal** (jitter, horizontal sync) take the distorted UV; effects that model the **glass** (the shadow mask, edge fade) derive position from `gl_FragCoord` so they stay fixed to the physical screen. The mask additionally divides by `u_pixelRatio` so its pitch is constant in CSS pixels rather than device pixels -- otherwise it shrinks to near-invisibility on a high-density display and resizes when the window moves between monitors.

All parameters are WebGL uniforms updated in real time as you drag the sliders. An animated `time` uniform drives the noise, flicker, jitter and glowing line.
