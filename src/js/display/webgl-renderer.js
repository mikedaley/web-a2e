/*
 * webgl-renderer.js - WebGL renderer for emulator display
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { VERSION } from "../config/version.js";
import { buildNoSignalFrame } from "./no-signal-frame.js";
import { machineDisplay } from "../machine/machine-profile.js";

export class WebGLRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = null;
    this.program = null;
    this.texture = null;

    // Burn-in resources
    this.burnInProgram = null;
    this.burnInFramebuffers = [null, null];
    this.burnInTextures = [null, null];
    this.currentBurnInIndex = 0;

    // Edge overlay program (second pass)
    this.edgeProgram = null;

    // Selection overlay texture
    this.selectionTexture = null;

    // Texture dimensions. The source texture is the machine's framebuffer, so
    // its size is the machine's, not a constant of the renderer.
    const display = machineDisplay();
    this.width = display.width;
    this.height = display.height;

    // CRT effect parameters (0.0 to 1.0 unless noted)
    this.crtParams = {
      // Screen geometry
      curvature: 0.2,

      // Scanlines and rasterization
      scanlineIntensity: 0.03,
      scanlineWidth: 0.25,
      // How much a bright line's beam spot widens over a dark one's
      beamBloom: 0.6,
      shadowMask: 0.3,
      // Mask geometry: 0 = aperture grille (stripes), 1 = shadow mask (triad)
      maskType: 0,

      // Glow/bloom
      glowIntensity: 0.0,
      glowSpread: 0.5,

      // Color adjustments
      brightness: 1.0, // 0.5 to 1.5
      contrast: 1.0, // 0.5 to 1.5
      saturation: 1.0, // 0.0 to 2.0

      // Vignette
      vignette: 0.0,

      // Chromatic aberration
      rgbOffset: 0.0,

      // New effects from cool-retro-term
      staticNoise: 0.0, // Static noise/grain
      flicker: 0.0, // Brightness flicker
      jitter: 0.0, // Random pixel displacement
      horizontalSync: 0.0, // Horizontal sync distortion
      glowingLine: 0.0, // Moving scan beam
      ambientLight: 0.0, // Screen surface reflection
      burnIn: 0.0, // Phosphor persistence

      // Screen border/overscan
      overscan: 0.0, // Border around display content (0.0 to 1.0)

      // Color bleed - vertical inter-scanline blending (CRT phosphor overlap)
      colorBleed: 0.8, // 0.0 to 1.0

      // Monochrome mode (0=color, 1=green, 2=amber, 3=white)
      monochromeMode: 0,

      // Corner radius for rounded screen corners (0.0 to 0.15)
      cornerRadius: 0.02,

      // Screen margin - insets content so rounded corners don't clip it
      // Should be slightly larger than cornerRadius
      screenMargin: 0.02,

      // Edge highlight intensity (0.0 to 1.0)
      edgeHighlight: 0.3,

      // Beam position crosshair (-1.0 = off, 0.0–1.0 = normalized position)
      beamY: -1.0,
      beamX: -1.0,

      // Screen inset — shrinks screen to reveal bezel without curvature
      screenInset: 0.0,

      // Bezel
      surroundColor: [0.784, 0.722, 0.604],
    };

    // Time for animated effects
    this.time = 0;

    // Uniform locations
    this.uniforms = {};
    this.burnInUniforms = {};
  }

  async init() {
    // Get WebGL context
    const ctxAttrs = {
      alpha: true,
      premultipliedAlpha: false,
      preserveDrawingBuffer: true,
    };
    this.gl =
      this.canvas.getContext("webgl2", ctxAttrs) ||
      this.canvas.getContext("webgl", ctxAttrs);
    if (!this.gl) {
      throw new Error("WebGL not supported");
    }

    const gl = this.gl;

    // Load shader sources from files
    const [vertexSource, fragmentSource, burnInSource, edgeSource] =
      await Promise.all([
        this.loadShader("shaders/vertex.glsl"),
        this.loadShader("shaders/crt.glsl"),
        this.loadShader("shaders/burnin.glsl"),
        this.loadShader("shaders/edge.glsl"),
      ]);

    // Create main shaders
    const vertexShader = this.compileShader(gl.VERTEX_SHADER, vertexSource);
    const fragmentShader = this.compileShader(
      gl.FRAGMENT_SHADER,
      fragmentSource,
    );

    // Create main program
    this.program = gl.createProgram();
    gl.attachShader(this.program, vertexShader);
    gl.attachShader(this.program, fragmentShader);
    gl.linkProgram(this.program);

    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
      throw new Error(
        "Shader program failed to link: " + gl.getProgramInfoLog(this.program),
      );
    }

    // Create burn-in shaders
    const burnInVertexShader = this.compileShader(
      gl.VERTEX_SHADER,
      vertexSource,
    );
    const burnInFragmentShader = this.compileShader(
      gl.FRAGMENT_SHADER,
      burnInSource,
    );

    // Create burn-in program
    this.burnInProgram = gl.createProgram();
    gl.attachShader(this.burnInProgram, burnInVertexShader);
    gl.attachShader(this.burnInProgram, burnInFragmentShader);
    gl.linkProgram(this.burnInProgram);

    if (!gl.getProgramParameter(this.burnInProgram, gl.LINK_STATUS)) {
      throw new Error(
        "Burn-in shader program failed to link: " +
          gl.getProgramInfoLog(this.burnInProgram),
      );
    }

    // Create edge overlay shaders
    const edgeVertexShader = this.compileShader(gl.VERTEX_SHADER, vertexSource);
    const edgeFragmentShader = this.compileShader(
      gl.FRAGMENT_SHADER,
      edgeSource,
    );

    // Create edge overlay program
    this.edgeProgram = gl.createProgram();
    gl.attachShader(this.edgeProgram, edgeVertexShader);
    gl.attachShader(this.edgeProgram, edgeFragmentShader);
    gl.linkProgram(this.edgeProgram);

    if (!gl.getProgramParameter(this.edgeProgram, gl.LINK_STATUS)) {
      throw new Error(
        "Edge shader program failed to link: " +
          gl.getProgramInfoLog(this.edgeProgram),
      );
    }

    // Get attribute locations
    this.positionLoc = gl.getAttribLocation(this.program, "a_position");
    this.texCoordLoc = gl.getAttribLocation(this.program, "a_texCoord");

    // Get burn-in attribute locations
    this.burnInPositionLoc = gl.getAttribLocation(
      this.burnInProgram,
      "a_position",
    );
    this.burnInTexCoordLoc = gl.getAttribLocation(
      this.burnInProgram,
      "a_texCoord",
    );

    // Get edge overlay attribute locations
    this.edgePositionLoc = gl.getAttribLocation(this.edgeProgram, "a_position");
    this.edgeTexCoordLoc = gl.getAttribLocation(this.edgeProgram, "a_texCoord");

    // Get all uniform locations for main program
    this.uniforms = {
      texture: gl.getUniformLocation(this.program, "u_texture"),
      burnInTexture: gl.getUniformLocation(this.program, "u_burnInTexture"),
      resolution: gl.getUniformLocation(this.program, "u_resolution"),
      textureSize: gl.getUniformLocation(this.program, "u_textureSize"),
      time: gl.getUniformLocation(this.program, "u_time"),
      curvature: gl.getUniformLocation(this.program, "u_curvature"),
      scanlineIntensity: gl.getUniformLocation(
        this.program,
        "u_scanlineIntensity",
      ),
      scanlineWidth: gl.getUniformLocation(this.program, "u_scanlineWidth"),
      beamBloom: gl.getUniformLocation(this.program, "u_beamBloom"),
      shadowMask: gl.getUniformLocation(this.program, "u_shadowMask"),
      maskType: gl.getUniformLocation(this.program, "u_maskType"),
      pixelRatio: gl.getUniformLocation(this.program, "u_pixelRatio"),
      glowIntensity: gl.getUniformLocation(this.program, "u_glowIntensity"),
      glowSpread: gl.getUniformLocation(this.program, "u_glowSpread"),
      brightness: gl.getUniformLocation(this.program, "u_brightness"),
      contrast: gl.getUniformLocation(this.program, "u_contrast"),
      saturation: gl.getUniformLocation(this.program, "u_saturation"),
      vignette: gl.getUniformLocation(this.program, "u_vignette"),
      flicker: gl.getUniformLocation(this.program, "u_flicker"),
      rgbOffset: gl.getUniformLocation(this.program, "u_rgbOffset"),
      staticNoise: gl.getUniformLocation(this.program, "u_staticNoise"),
      jitter: gl.getUniformLocation(this.program, "u_jitter"),
      horizontalSync: gl.getUniformLocation(this.program, "u_horizontalSync"),
      glowingLine: gl.getUniformLocation(this.program, "u_glowingLine"),
      ambientLight: gl.getUniformLocation(this.program, "u_ambientLight"),
      burnIn: gl.getUniformLocation(this.program, "u_burnIn"),
      overscan: gl.getUniformLocation(this.program, "u_overscan"),
      colorBleed: gl.getUniformLocation(this.program, "u_colorBleed"),
      monochromeMode: gl.getUniformLocation(this.program, "u_monochromeMode"),
      cornerRadius: gl.getUniformLocation(this.program, "u_cornerRadius"),
      screenMargin: gl.getUniformLocation(this.program, "u_screenMargin"),
      beamY: gl.getUniformLocation(this.program, "u_beamY"),
      beamX: gl.getUniformLocation(this.program, "u_beamX"),
      selectionTexture: gl.getUniformLocation(
        this.program,
        "u_selectionTexture",
      ),
      screenInset: gl.getUniformLocation(this.program, "u_screenInset"),
      surroundColor: gl.getUniformLocation(this.program, "u_surroundColor"),
    };

    // Get burn-in program uniform locations
    this.burnInUniforms = {
      currentTexture: gl.getUniformLocation(
        this.burnInProgram,
        "u_currentTexture",
      ),
      previousTexture: gl.getUniformLocation(
        this.burnInProgram,
        "u_previousTexture",
      ),
      burnInTau: gl.getUniformLocation(this.burnInProgram, "u_burnInTau"),
      deltaTime: gl.getUniformLocation(this.burnInProgram, "u_deltaTime"),
      monochromeMode: gl.getUniformLocation(this.burnInProgram, "u_monochromeMode"),
    };

    // Get edge overlay program uniform locations
    this.edgeUniforms = {
      curvature: gl.getUniformLocation(this.edgeProgram, "u_curvature"),
      cornerRadius: gl.getUniformLocation(this.edgeProgram, "u_cornerRadius"),
      edgeHighlight: gl.getUniformLocation(this.edgeProgram, "u_edgeHighlight"),
      screenInset: gl.getUniformLocation(this.edgeProgram, "u_screenInset"),
      textureSize: gl.getUniformLocation(this.edgeProgram, "u_textureSize"),
      resolution: gl.getUniformLocation(this.edgeProgram, "u_resolution"),
    };

    // Create vertex buffer (full-screen quad)
    // Format: x, y, u, v - texture coords are flipped for screen rendering
    const positions = new Float32Array([
      -1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0,
    ]);

    this.vertexBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);

    // Create vertex buffer for framebuffer rendering (non-flipped texture coords)
    const fbPositions = new Float32Array([
      -1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1,
    ]);

    this.fbVertexBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.fbVertexBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, fbPositions, gl.STATIC_DRAW);

    // Framebuffer-sized textures. Extracted so a machine whose picture is a
    // different size can have them rebuilt without re-running the whole of
    // init() — see setMachineDisplay().
    this.initTextures();

    // Set initial canvas size if not already set
    if (!this.canvas.width || !this.canvas.height) {
      this.canvas.width = this.width;
      this.canvas.height = this.height;
    }

    // Set viewport
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);

    // Enable blending for rounded corners transparency
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  }

  /**
   * Create (or re-create) every texture sized to the machine's framebuffer:
   * the source texture, the burn-in ping-pong pair and their framebuffers,
   * and the selection overlay. Safe to call again after a size change; the
   * previous objects are deleted first so nothing is leaked.
   */
  initTextures() {
    const gl = this.gl;
    if (!gl) return;

    if (this.texture) gl.deleteTexture(this.texture);
    if (this.selectionTexture) gl.deleteTexture(this.selectionTexture);
    for (let i = 0; i < 2; i++) {
      if (this.burnInTextures?.[i]) gl.deleteTexture(this.burnInTextures[i]);
      if (this.burnInFramebuffers?.[i]) {
        gl.deleteFramebuffer(this.burnInFramebuffers[i]);
      }
    }

    // Create main texture
    this.texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    // Nearest neighbour by default (sharp pixels), but a re-create after a
    // machine change must not throw away a filter the user chose.
    if (this.useNearestFilter === undefined) this.useNearestFilter = true;
    const filter = this.useNearestFilter ? gl.NEAREST : gl.LINEAR;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);

    // Initialize with empty texture
    const emptyData = new Uint8Array(this.width * this.height * 4);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      this.width,
      this.height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      emptyData,
    );

    // Create burn-in framebuffers and textures (ping-pong)
    for (let i = 0; i < 2; i++) {
      this.burnInTextures[i] = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, this.burnInTextures[i]);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA,
        this.width,
        this.height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        emptyData,
      );

      this.burnInFramebuffers[i] = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.burnInFramebuffers[i]);
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        this.burnInTextures[i],
        0,
      );
    }

    // Unbind framebuffer
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // Create selection overlay texture
    this.selectionTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.selectionTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      this.width,
      this.height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      emptyData,
    );

  }

  compileShader(type, source) {
    const gl = this.gl;
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);

    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      throw new Error(
        "Shader compilation failed: " + gl.getShaderInfoLog(shader),
      );
    }

    return shader;
  }

  updateTexture(data) {
    const gl = this.gl;

    // While the no-signal screen is up it owns the texture. A frame can still
    // arrive from the Worker just after power-off; without this it would land
    // on top of the message and leave the last emulator frame frozen on screen.
    if (this._noSignal && data !== this._noSignalFrame) return;

    // Frames normally arrive as a view onto a SharedArrayBuffer and upload
    // straight from it. Current browsers accept a shared view here, but the
    // restriction was only lifted relatively recently — so if one refuses,
    // fall back permanently to a single reusable staging copy rather than
    // throwing a frame away. The staging buffer is allocated once, so even the
    // fallback stays allocation-free per frame.
    if (this._needsTextureStaging) {
      data = this._stageTextureData(data);
    }

    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    try {
      gl.texSubImage2D(
        gl.TEXTURE_2D,
        0,
        0,
        0,
        this.width,
        this.height,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        data,
      );
    } catch (e) {
      if (this._needsTextureStaging) throw e;
      console.warn("WebGL rejected a shared texture source; staging copies instead", e);
      this._needsTextureStaging = true;
      this.updateTexture(data);
    }
  }

  _stageTextureData(data) {
    if (!this._textureStaging || this._textureStaging.length !== data.length) {
      this._textureStaging = new Uint8Array(data.length);
    }
    this._textureStaging.set(data);
    return this._textureStaging;
  }

  updateSelectionTexture(canvas) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.selectionTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas);
  }

  updateBurnIn() {
    const gl = this.gl;

    if (this.crtParams.burnIn < 0.001) return;

    // Swap buffers
    const prevIndex = this.currentBurnInIndex;
    this.currentBurnInIndex = 1 - this.currentBurnInIndex;
    const currIndex = this.currentBurnInIndex;

    // Render to current burn-in buffer
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.burnInFramebuffers[currIndex]);
    gl.viewport(0, 0, this.width, this.height);

    gl.useProgram(this.burnInProgram);

    // Bind framebuffer vertex buffer (non-flipped texture coords)
    gl.bindBuffer(gl.ARRAY_BUFFER, this.fbVertexBuffer);
    gl.enableVertexAttribArray(this.burnInPositionLoc);
    gl.vertexAttribPointer(this.burnInPositionLoc, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(this.burnInTexCoordLoc);
    gl.vertexAttribPointer(this.burnInTexCoordLoc, 2, gl.FLOAT, false, 16, 8);

    // Bind current frame texture
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.uniform1i(this.burnInUniforms.currentTexture, 0);

    // Bind previous burn-in texture
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.burnInTextures[prevIndex]);
    gl.uniform1i(this.burnInUniforms.previousTexture, 1);

    // Time constant in seconds — higher burnIn holds the image longer. The
    // slider is the phosphor's persistence, not a per-frame subtraction.
    const tau = 0.06 + this.crtParams.burnIn * 0.9;
    gl.uniform1f(this.burnInUniforms.burnInTau, tau);

    // Real elapsed time since the last accumulation pass. Clamped because a
    // backgrounded tab can return with an arbitrarily large gap, and because
    // the first pass has no previous timestamp to measure from.
    const now = performance.now() * 0.001;
    const dt = this._lastBurnInTime === undefined
      ? 1 / 15
      : Math.min(now - this._lastBurnInTime, 1.0);
    this._lastBurnInTime = now;
    gl.uniform1f(this.burnInUniforms.deltaTime, dt);

    // A monochrome tube has one phosphor, so its channels decay together.
    gl.uniform1i(this.burnInUniforms.monochromeMode, this.crtParams.monochromeMode);

    // Draw
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    // Unbind framebuffer and restore viewport
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
  }

  draw() {
    const gl = this.gl;

    // Apply any pending canvas resize right before painting so the
    // buffer clear and the redraw happen in the same frame (no flicker).
    // Re-derive the buffer size when the display's pixel density changes.
    // Dragging the window from a Retina display to a 1x monitor changes
    // devicePixelRatio without changing any CSS size, so the ResizeObserver
    // that normally drives resize() never fires and the drawing buffer keeps
    // the density it was allocated with — leaving the picture over- or
    // under-sampled until some unrelated resize happens to correct it. There is
    // no event for this (the matchMedia idiom does not fire reliably), so it is
    // a comparison here: one float check per frame against a value the shader
    // already needs.
    const dpr = window.devicePixelRatio || 1;
    if (dpr !== this._bufferDpr && this._cssWidth !== undefined) {
      this._pendingWidth = Math.floor(this._cssWidth * dpr);
      this._pendingHeight = Math.floor(this._cssHeight * dpr);
    }

    if (this._pendingWidth !== undefined) {
      const pw = this._pendingWidth;
      const ph = this._pendingHeight;
      this._pendingWidth = undefined;
      this._pendingHeight = undefined;
      this._bufferDpr = dpr;
      if (this.canvas.width !== pw || this.canvas.height !== ph) {
        this.canvas.width = pw;
        this.canvas.height = ph;
        gl.viewport(0, 0, pw, ph);
      }
    }

    // Update time for animated effects using real elapsed time
    const now = performance.now() * 0.001;
    if (this._lastTime === undefined) this._lastTime = now;
    this.time += now - this._lastTime;
    this._lastTime = now;

    // Update burn-in accumulation (throttled to every 4th frame)
    this._burnInCounter = (this._burnInCounter || 0) + 1;
    if (this._burnInCounter % 4 === 0) {
      this.updateBurnIn();
    }

    gl.clearColor(0, 0, 0, 0); // Black background
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this.program);

    // Bind vertex buffer
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);

    // Position attribute
    gl.enableVertexAttribArray(this.positionLoc);
    gl.vertexAttribPointer(this.positionLoc, 2, gl.FLOAT, false, 16, 0);

    // TexCoord attribute
    gl.enableVertexAttribArray(this.texCoordLoc);
    gl.vertexAttribPointer(this.texCoordLoc, 2, gl.FLOAT, false, 16, 8);

    // Bind main texture
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.uniform1i(this.uniforms.texture, 0);

    // Bind burn-in texture
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.burnInTextures[this.currentBurnInIndex]);
    gl.uniform1i(this.uniforms.burnInTexture, 1);

    // Bind selection overlay texture
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, this.selectionTexture);
    gl.uniform1i(this.uniforms.selectionTexture, 2);

    // Per-frame uniforms (always update)
    gl.uniform1f(this.uniforms.time, this.time);
    gl.uniform1f(this.uniforms.beamY, this.crtParams.beamY);
    gl.uniform1f(this.uniforms.beamX, this.crtParams.beamX);
    gl.uniform2f(
      this.uniforms.resolution,
      this.canvas.width,
      this.canvas.height,
    );
    // Per-frame, not cached: dragging the window to a display of a different
    // density changes this with no resize and no parameter change, and the mask
    // pitch depends on it.
    gl.uniform1f(this.uniforms.pixelRatio, window.devicePixelRatio || 1);

    // Static uniforms (only update when CRT parameters change)
    if (this._uniformsDirty !== false) {
      this._uniformsDirty = false;
      gl.uniform2f(this.uniforms.textureSize, this.width, this.height);
      gl.uniform1f(this.uniforms.curvature, this.crtParams.curvature);
      gl.uniform1f(
        this.uniforms.scanlineIntensity,
        this.crtParams.scanlineIntensity,
      );
      gl.uniform1f(this.uniforms.scanlineWidth, this.crtParams.scanlineWidth);
      gl.uniform1f(this.uniforms.beamBloom, this.crtParams.beamBloom);
      gl.uniform1f(this.uniforms.shadowMask, this.crtParams.shadowMask);
      gl.uniform1i(this.uniforms.maskType, this.crtParams.maskType);
      gl.uniform1f(this.uniforms.glowIntensity, this.crtParams.glowIntensity);
      gl.uniform1f(this.uniforms.glowSpread, this.crtParams.glowSpread);
      gl.uniform1f(this.uniforms.brightness, this.crtParams.brightness);
      gl.uniform1f(this.uniforms.contrast, this.crtParams.contrast);
      gl.uniform1f(this.uniforms.saturation, this.crtParams.saturation);
      gl.uniform1f(this.uniforms.vignette, this.crtParams.vignette);
      gl.uniform1f(this.uniforms.flicker, this.crtParams.flicker);
      gl.uniform1f(this.uniforms.rgbOffset, this.crtParams.rgbOffset);
      gl.uniform1f(this.uniforms.staticNoise, this.crtParams.staticNoise);
      gl.uniform1f(this.uniforms.jitter, this.crtParams.jitter);
      gl.uniform1f(this.uniforms.horizontalSync, this.crtParams.horizontalSync);
      gl.uniform1f(this.uniforms.glowingLine, this.crtParams.glowingLine);
      gl.uniform1f(this.uniforms.ambientLight, this.crtParams.ambientLight);
      gl.uniform1f(this.uniforms.burnIn, this.crtParams.burnIn);
      gl.uniform1f(this.uniforms.overscan, this.crtParams.overscan);
      gl.uniform1f(this.uniforms.colorBleed, this.crtParams.colorBleed);
      gl.uniform1i(this.uniforms.monochromeMode, this.crtParams.monochromeMode);
      gl.uniform1f(this.uniforms.cornerRadius, (this.crtParams.screenInset > 0 || this.crtParams.curvature > 0) ? this.crtParams.cornerRadius : 0.0);
      gl.uniform1f(this.uniforms.screenMargin, this.crtParams.screenMargin);
      gl.uniform1f(this.uniforms.screenInset, this.crtParams.screenInset);
      gl.uniform3fv(this.uniforms.surroundColor, this.crtParams.surroundColor);
    }

    // Draw main CRT pass
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    // --- Second pass: edge overlay (unaffected by CRT effects) ---
    if (this.crtParams.edgeHighlight > 0.001) {
      gl.useProgram(this.edgeProgram);

      // Reuse the same vertex buffer (same full-screen quad)
      gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
      gl.enableVertexAttribArray(this.edgePositionLoc);
      gl.vertexAttribPointer(this.edgePositionLoc, 2, gl.FLOAT, false, 16, 0);
      gl.enableVertexAttribArray(this.edgeTexCoordLoc);
      gl.vertexAttribPointer(this.edgeTexCoordLoc, 2, gl.FLOAT, false, 16, 8);

      // Set edge uniforms
      gl.uniform1f(this.edgeUniforms.curvature, this.crtParams.curvature);
      gl.uniform1f(this.edgeUniforms.cornerRadius, (this.crtParams.screenInset > 0 || this.crtParams.curvature > 0) ? this.crtParams.cornerRadius : 0.0);
      gl.uniform1f(
        this.edgeUniforms.edgeHighlight,
        this.crtParams.edgeHighlight,
      );
      gl.uniform1f(this.edgeUniforms.screenInset, this.crtParams.screenInset);
      gl.uniform2f(this.edgeUniforms.textureSize, this.width, this.height);
      gl.uniform2f(
        this.edgeUniforms.resolution,
        this.canvas.width,
        this.canvas.height,
      );

      // Draw edge overlay (alpha-blended on top of CRT output)
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }
  }

  clear() {
    const gl = this.gl;

    // Clear the texture to black
    const emptyData = new Uint8Array(this.width * this.height * 4);
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.texSubImage2D(
      gl.TEXTURE_2D,
      0,
      0,
      0,
      this.width,
      this.height,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      emptyData,
    );

    // Clear burn-in buffers
    for (let i = 0; i < 2; i++) {
      gl.bindTexture(gl.TEXTURE_2D, this.burnInTextures[i]);
      gl.texSubImage2D(
        gl.TEXTURE_2D,
        0,
        0,
        0,
        this.width,
        this.height,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        emptyData,
      );
    }

    // Clear and redraw
    gl.clearColor(0, 0, 0, 0); // Black background
    gl.clear(gl.COLOR_BUFFER_BIT);
    this.draw();
  }

  // Set individual CRT parameter
  setParam(name, value) {
    if (name in this.crtParams && this.crtParams[name] !== value) {
      this.crtParams[name] = value;
      this._uniformsDirty = true;
    }
  }

  // Set multiple CRT parameters at once
  setParams(params) {
    for (const [name, value] of Object.entries(params)) {
      if (name in this.crtParams && this.crtParams[name] !== value) {
        this.crtParams[name] = value;
        this._uniformsDirty = true;
      }
    }
  }

  // Set texture filtering mode
  setNearestFilter(enabled) {
    const gl = this.gl;
    this.useNearestFilter = enabled;

    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    if (enabled) {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    } else {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    }
  }

  // Legacy method for compatibility
  setCRTEnabled(enabled) {
    // Apply preset CRT settings
    if (enabled) {
      this.setParams({ curvature: 0.3, scanlineIntensity: 0.3, shadowMask: 0.2, vignette: 0.2, glowIntensity: 0.1 });
    } else {
      this.setParams({ curvature: 0, scanlineIntensity: 0, shadowMask: 0, vignette: 0, glowIntensity: 0 });
    }
  }

  /**
   * Enter or leave the powered-off "no signal" screen.
   *
   * The message is uploaded as the source texture rather than drawn by the
   * shader, so it picks up the whole CRT chain — RGB shift, colour bleed,
   * scanlines, mask, glow — and reads as a real picture. It does not pass
   * through the core's NTSC decoder, which sees only emulator video. Leaving
   * the mode uploads nothing: the first real frame overwrites it.
   */
  /**
   * Adopt a different machine's framebuffer geometry.
   *
   * The source texture is the machine's framebuffer, so a machine with a
   * different picture needs the texture, the burn-in pair and the selection
   * overlay rebuilt at the new size. Nothing to do when the size is unchanged,
   * which is the case for every machine modelled so far — the //e and the II+
   * emit the same 560 dots across the same 192 doubled lines.
   */
  /**
   * The machine the picture belongs to: its size, and its name for the
   * powered-off screen, which says which machine to switch on.
   */
  setMachine(profile) {
    if (!profile) return;
    const name = profile.shortName || "//e";
    if (name !== this._machineName) {
      this._machineName = name;
      this._noSignalFrame = null;
      if (this._noSignal && this.gl) this.setNoSignal(true);
    }
    this.setMachineDisplay(profile.display);
  }

  setMachineDisplay(display) {
    if (!display || !display.width || !display.height) return;
    if (display.width === this.width && display.height === this.height) return;

    this.width = display.width;
    this.height = display.height;

    // The powered-off picture is drawn at the framebuffer size, so it has to
    // be rebuilt too rather than stretched.
    this._noSignalFrame = null;

    if (this.gl) {
      this.initTextures();
      if (this._noSignal) this.setNoSignal(true);
    }
  }

  setNoSignal(enabled) {
    this._noSignal = enabled;
    if (!enabled) return;

    if (!this._noSignalFrame) {
      this._noSignalFrame = buildNoSignalFrame(
        this.width,
        this.height,
        this._machineName || "//e",
      );
    }
    this.updateTexture(this._noSignalFrame);
  }

  resize(width, height) {
    // Defer the actual canvas buffer resize to the next draw() call.
    // Setting canvas.width/height clears the WebGL drawing buffer and forces
    // a GPU reallocation.  During a drag-resize this is called on every
    // mousemove, but draw() only runs once per rAF.  Deferring keeps the
    // buffer clear and the repaint in the same frame, eliminating flicker.
    const dpr = window.devicePixelRatio || 1;
    this._cssWidth = width;
    this._cssHeight = height;
    this._pendingWidth = Math.floor(width * dpr);
    this._pendingHeight = Math.floor(height * dpr);
  }

  /**
   * Fetch a shader source file.
   *
   * The ?v= is not decoration. Unlike the JS and CSS bundles, shader files keep
   * the same URL from one build to the next, so a browser that has one in its
   * HTTP cache will happily keep using it after a deploy — which is exactly how
   * 1.1.12 shipped a rewritten crt.glsl that nobody saw. Stamping the app
   * version into the URL makes every release a fresh resource. The service
   * worker precaches the same versioned URLs (see PRECACHE_ASSETS in sw.js), so
   * offline launches still find them.
   */
  async loadShader(path) {
    const url = `${path}?v=${VERSION}`;
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Failed to load shader: ${path}`);
    }
    return response.text();
  }
}
