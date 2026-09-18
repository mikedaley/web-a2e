/*
 * display-settings-window.js - Display settings window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { showConfirm, showPrompt } from "../ui/confirm.js";
import { showToast } from "../ui/toast.js";
import { escapeHtml } from "../utils/string-utils.js";
import { getMachineProfile } from "../machine/machine-profile.js";
import {
  LEGACY_DISPLAY_SETTINGS_KEY,
  defaultScreenBorder,
  displaySettingsKey,
  inheritsLegacySettings,
} from "./display-storage.js";
import {
  captureProfileValues,
  deleteProfile,
  findProfile,
  isProfileId,
  loadProfiles,
  saveProfiles,
  upsertProfile,
  validateProfileName,
} from "./display-profiles.js";

/**
 * Which decoder the core runs over the machine's 14.31818 MHz dot stream.
 *
 * The Apple //e emits one bit per dot and nothing else — every colour is
 * manufactured by the receiver. These four are four receivers, and they must
 * stay in step with VideoColorMode in src/core/types.hpp.
 */
export const COLOR_MODE = {
  MONOCHROME: 0,
  PIXEL_EXACT: 1,
  RGB_MONITOR: 2,
  COMPOSITE: 3,
};

/**
 * DisplaySettingsWindow - CRT display effects and settings
 */
export class DisplaySettingsWindow extends BaseWindow {
  constructor(renderer, wasmModule) {
    super({
      id: "display-settings",
      title: "Display Settings",
      minWidth: 260,
      minHeight: 240,
      defaultWidth: 300,
      // Sized for the collapsed window. Opening Advanced grows it to fit.
      defaultHeight: 330,
    });

    this.renderer = renderer;
    this.wasmModule = wasmModule;

    // Profiles the user has saved. Loaded here rather than in create() so the
    // Monitor dropdown has them the first time it renders.
    this.userProfiles = loadProfiles();

    // Whether the selected profile has edits that are not in it yet. Only
    // meaningful while a profile is selected; a built-in preset drops to
    // Custom the moment it is edited and so is never "modified".
    this.profileDirty = false;

    // Timer for the transient "Saved" confirmation in the description line.
    this.statusTimer = null;

    // Monitor presets. Each names a real thing a //e was plugged into, and
    // sets the whole picture in one go — the sliders underneath are the
    // advanced view of whatever the preset just chose.
    //
    // Presets deliberately do not touch brightness, contrast, saturation, the
    // bezel or the screen border. Those are the user's own calibration and
    // framing, not properties of the monitor being imitated, and silently
    // resetting them when someone switches preset would be obnoxious.
    this.monitorPresets = [
      {
        id: "flat",
        label: "Pixel Exact",
        description: "No CRT simulation — sharp square pixels.",
        values: {
          curvature: 0, scanlines: 0, beamBloom: 60,
          shadowMask: 0, maskType: 0, phosphorGlow: 0, vignette: 0,
          rgbOffset: 0, flicker: 0, staticNoise: 0, jitter: 0,
          horizontalSync: 0, glowingLine: 0, ambientLight: 0, burnIn: 0,
          colorBleed: 0, monochromeMode: 0, sharpPixels: true,
          colorMode: COLOR_MODE.PIXEL_EXACT,
        },
      },
      {
        id: "composite",
        label: "Composite Color",
        description: "Colour TV or composite monitor — soft, with artefact fringing.",
        values: {
          // Flat. A real colour set had a curved tube, but geometry is not what
          // this preset is for — the composite look is the decoding, and the
          // barrel distortion mostly gets in the way of reading the picture.
          // Curvature is still there under Advanced for anyone who wants it.
          curvature: 0, scanlines: 30, beamBloom: 60,
          // A consumer colour set used a dot triad, not a grille.
          shadowMask: 30, maskType: 1, phosphorGlow: 15, vignette: 20,
          rgbOffset: 6, flicker: 0, staticNoise: 0, jitter: 0,
          horizontalSync: 0, glowingLine: 0, ambientLight: 0, burnIn: 10,
          // The core now demodulates the real signal, so the picture arrives
          // already soft and already fringed. This is only phosphor overlap.
          colorBleed: 30, monochromeMode: 0, sharpPixels: false,
          colorMode: COLOR_MODE.COMPOSITE,
        },
      },
      {
        id: "rgb",
        label: "RGB Monitor",
        description: "Separate colour signals — sharp, no composite artefacts.",
        values: {
          curvature: 10, scanlines: 22, beamBloom: 45,
          shadowMask: 22, maskType: 0, phosphorGlow: 8, vignette: 12,
          rgbOffset: 0, flicker: 0, staticNoise: 0, jitter: 0,
          horizontalSync: 0, glowingLine: 0, ambientLight: 0, burnIn: 5,
          // No encoding to decode, so no fringing and almost no chroma bleed.
          colorBleed: 15, monochromeMode: 0, sharpPixels: true,
          colorMode: COLOR_MODE.RGB_MONITOR,
        },
      },
      {
        id: "green",
        label: "Monochrome Green",
        description: "P1 phosphor — long persistence, no mask.",
        values: {
          curvature: 20, scanlines: 32, beamBloom: 70,
          // A monochrome tube has one continuous phosphor coating and no
          // aperture at all — a mask exists only to keep three beams apart.
          shadowMask: 0, maskType: 0, phosphorGlow: 28, vignette: 25,
          rgbOffset: 0, flicker: 0, staticNoise: 0, jitter: 0,
          horizontalSync: 0, glowingLine: 0, ambientLight: 0, burnIn: 40,
          colorBleed: 25, monochromeMode: 1, sharpPixels: false,
          colorMode: COLOR_MODE.MONOCHROME,
        },
      },
      {
        id: "amber",
        label: "Monochrome Amber",
        description: "P3 phosphor — the warmer of the two mono tubes.",
        values: {
          curvature: 20, scanlines: 32, beamBloom: 70,
          shadowMask: 0, maskType: 0, phosphorGlow: 25, vignette: 25,
          rgbOffset: 0, flicker: 0, staticNoise: 0, jitter: 0,
          horizontalSync: 0, glowingLine: 0, ambientLight: 0, burnIn: 35,
          colorBleed: 25, monochromeMode: 2, sharpPixels: false,
          colorMode: COLOR_MODE.MONOCHROME,
        },
      },
    ];

    // Monochrome mode options
    this.maskTypes = [
      { value: 0, label: "Aperture Grille" },
      { value: 1, label: "Shadow Mask" },
    ];

    this.monochromeModes = [
      { value: 0, label: "Color" },
      { value: 1, label: "Green" },
      { value: 2, label: "Amber" },
      { value: 3, label: "White" },
    ];

    // Default values (percentages 0-100 for UI, converted to shader values)
    // All effects off by default except basic image adjustments. The one
    // default that differs by machine — the screen border — is filled in by
    // defaultsFor(), so `defaults` is read through that rather than directly.
    this.defaults = {
      // "flat" reproduces what this window has always shipped with — every
      // effect off — so existing users see no change until they pick a monitor.
      preset: "flat",
      curvature: 0,
      scanlines: 0,
      // Beam bloom is a property of the spot, not an effect of its own: it only
      // shows through the scanline comb, so it ships on rather than at zero.
      beamBloom: 60,
      shadowMask: 0,
      // 0 = aperture grille (stripes), 1 = shadow mask (dot triad)
      maskType: 0,
      phosphorGlow: 0,
      vignette: 0,
      brightness: 100,
      contrast: 100,
      saturation: 100,
      rgbOffset: 0,
      flicker: 0,
      staticNoise: 0,
      jitter: 0,
      horizontalSync: 0,
      glowingLine: 0,
      ambientLight: 0,
      burnIn: 0,
      overscan: 0, // see defaultsFor()
      // True so the shipped defaults are exactly the "flat" preset. With this
      // false the window opened saying Pixel Exact while the pixels were being
      // smoothed by linear filtering, and the label was simply wrong.
      sharpPixels: true,
      // How hard the seam between two source dots is when the picture is
      // magnified. 0 is plain bilinear, which is what this has always been;
      // 100 confines the transition to one output pixel. See sharpenUV() in
      // crt.glsl. Defaults to 0 so nothing changes until it is asked for.
      sharpness: 0,
      // Color bleed (vertical inter-scanline blending)
      colorBleed: 0,
      // Monochrome mode (0=color, 1=green, 2=amber, 3=white)
      monochromeMode: 0,
      // Which decoder the core runs over the dot stream (see COLOR_MODE)
      colorMode: COLOR_MODE.PIXEL_EXACT,
      // Bezel
      screenInset: 0,
      bezelColor: "#c8b89a",
    };

    // Current values, which are the running machine's: every machine keeps
    // its own (display-storage.js), and a switch loads that machine's.
    this.settings = this.defaultsFor(getMachineProfile());

    // Slider info for rendering. `advanced` sections live behind the
    // disclosure; Image stays visible because it is calibration, not
    // simulation, and is the thing people reach for most often.
    this.sliderConfigs = [
      {
        section: "Image",
        advanced: false,
        sliders: [
          { id: "brightness", label: "Brightness", param: "brightness" },
          { id: "contrast", label: "Contrast", param: "contrast" },
          { id: "saturation", label: "Saturation", param: "saturation" },
        ],
      },
      {
        section: "CRT Effects",
        advanced: true,
        sliders: [
          { id: "curvature", label: "Screen Curvature", param: "curvature" },
          { id: "overscan", label: "Screen Border", param: "overscan" },
          { id: "scanlines", label: "Scanlines", param: "scanlineIntensity" },
          { id: "beamBloom", label: "Beam Bloom", param: "beamBloom" },
          { id: "shadowMask", label: "Shadow Mask", param: "shadowMask" },
          {
            id: "phosphorGlow",
            label: "Phosphor Glow",
            param: "glowIntensity",
          },
          { id: "vignette", label: "Vignette", param: "vignette" },
          { id: "rgbOffset", label: "RGB Offset", param: "rgbOffset" },
          { id: "flicker", label: "Flicker", param: "flicker" },
        ],
      },
      {
        section: "Analog Effects",
        advanced: true,
        sliders: [
          { id: "staticNoise", label: "Static Noise", param: "staticNoise" },
          { id: "jitter", label: "Jitter", param: "jitter" },
          {
            id: "horizontalSync",
            label: "Horizontal Sync",
            param: "horizontalSync",
          },
          { id: "glowingLine", label: "Glowing Line", param: "glowingLine" },
          { id: "ambientLight", label: "Ambient Light", param: "ambientLight" },
          { id: "burnIn", label: "Burn In", param: "burnIn" },
        ],
      },
      {
        section: "Bezel",
        advanced: true,
        sliders: [
          { id: "screenInset", label: "Bezel Width", param: "screenInset" },
        ],
      },
    ];
  }

  renderContent() {
    let html = '<div class="display-settings-content">';

    // Monitor preset — the primary control. Everything below it is the
    // detail of whatever this chooses.
    html += `
      <div class="settings-section">
        <div class="settings-section-title">Monitor</div>
        <select id="ds-preset" class="settings-select">
          ${this._renderPresetOptions()}
        </select>
        <div class="preset-description" id="ds-preset-description">${escapeHtml(this._presetDescription())}</div>
        <div class="profile-actions">
          <button id="ds-profile-update" class="settings-btn settings-btn-compact"
                  title="Save these changes back to the selected profile">Save</button>
          <button id="ds-profile-saveas" class="settings-btn settings-btn-compact"
                  title="Save the current picture as a new named profile">Save As…</button>
          <button id="ds-profile-delete" class="settings-btn settings-btn-compact settings-btn-danger"
                  title="Delete the selected profile">Delete</button>
        </div>
      </div>`;

    html += this._renderSliderSections(false);

    // Everything else is folded away. These are the knobs behind the preset,
    // not the everyday controls.
    html += `
      <div class="settings-section advanced-section">
        <div class="settings-section-title collapsible" id="ds-advanced-toggle">▶ Advanced</div>
        <div class="advanced-content hidden" id="ds-advanced-content">`;

    html += this._renderSliderSections(true);
    html += this._renderRenderingSection();
    html += this._renderPhosphorSection();

    html += `
        </div>
      </div>`;

    // Reset button
    html += `
      <div class="settings-actions">
        <button id="ds-reset" class="settings-btn">Reset to Defaults</button>
      </div>`;

    html += "</div>";
    return html;
  }

  /**
   * Options for the Monitor dropdown.
   *
   * The user's own profiles get their own group. Grouping them matters once
   * there are a few: a flat list mixes "Composite Color" — a claim about real
   * hardware — with "Mike's telly", and the difference between the two is worth
   * keeping visible.
   */
  _renderPresetOptions() {
    const selected = this.settings.preset;
    const option = (value, label) =>
      `<option value="${escapeHtml(value)}"${value === selected ? " selected" : ""}>${escapeHtml(label)}</option>`;

    let html = this.monitorPresets.map((p) => option(p.id, p.label)).join("");

    if (this.userProfiles.length) {
      html += `<optgroup label="My Profiles">${this.userProfiles
        .map((p) => option(p.id, p.name))
        .join("")}</optgroup>`;
    }

    // Custom is last: it is where you land by editing, not somewhere you go.
    html += option("custom", "Custom");
    return html;
  }

  /**
   * Description line for the currently selected preset.
   */
  _presetDescription() {
    const profile = findProfile(this.userProfiles, this.settings.preset);
    if (profile) {
      return this.profileDirty
        ? "Your saved profile — unsaved changes."
        : "Your saved profile.";
    }

    const preset = this.monitorPresets.find((p) => p.id === this.settings.preset);
    return preset ? preset.description : "Hand-tuned settings.";
  }

  /**
   * The values the current selection claims to own, whether built-in or saved.
   */
  _selectedPresetValues() {
    const profile = findProfile(this.userProfiles, this.settings.preset);
    if (profile) return profile.values;

    const preset = this.monitorPresets.find((p) => p.id === this.settings.preset);
    return preset ? preset.values : null;
  }

  /**
   * Render the slider sections matching the requested `advanced` flag.
   */
  _renderSliderSections(advanced) {
    let html = "";

    for (const section of this.sliderConfigs) {
      if (Boolean(section.advanced) !== advanced) continue;

      html += `<div class="settings-section">
        <div class="settings-section-title">${section.section}</div>`;

      for (const slider of section.sliders) {
        html += `
          <div class="setting-row">
            <label title="${slider.label}">${slider.label}</label>
            <input type="range" id="ds-${slider.id}" min="0" max="100" value="${this.settings[slider.id]}">
            <span class="setting-value" id="ds-val-${slider.id}">${this.settings[slider.id]}%</span>
          </div>`;
      }

      // Add color picker to the Bezel section
      if (section.section === "Bezel") {
        html += `
          <div class="setting-row">
            <label title="Bezel Color">Bezel Color</label>
            <input type="color" id="ds-bezelColor" value="${this.settings.bezelColor}">
          </div>`;
      }

      html += "</div>";
    }

    return html;
  }

  /**
   * Rendering section: mask geometry, phosphor colour, filtering.
   */
  _renderRenderingSection() {
    return `
      <div class="settings-section">
        <div class="settings-section-title">Rendering</div>
        <div class="setting-row">
          <label>Mask Type</label>
          <select id="ds-maskType" class="settings-select">
            ${this.maskTypes
              .map(
                (t) =>
                  `<option value="${t.value}" ${this.settings.maskType === t.value ? "selected" : ""}>${t.label}</option>`,
              )
              .join("")}
          </select>
        </div>
        <div class="setting-row">
          <label>Display Mode</label>
          <select id="ds-monochromeMode" class="settings-select">
            ${this.monochromeModes
              .map(
                (mode) =>
                  `<option value="${mode.value}" ${this.settings.monochromeMode === mode.value ? "selected" : ""}>${mode.label}</option>`,
              )
              .join("")}
          </select>
        </div>
        <div class="setting-row toggle-row">
          <label>Sharp Pixels</label>
          <label class="toggle">
            <input type="checkbox" id="ds-sharpPixels" ${this.settings.sharpPixels ? "checked" : ""}>
            <span class="toggle-slider"></span>
          </label>
        </div>
        <div class="setting-row">
          <label title="How hard the seam is between two source dots when the picture is magnified. 0 is plain bilinear; 100 keeps each dot flat and puts the whole transition in one output pixel. Has no effect with Sharp Pixels on, which is already hard.">Edge Sharpness</label>
          <input type="range" id="ds-sharpness" min="0" max="100" value="${this.settings.sharpness}">
          <span class="setting-value" id="ds-val-sharpness">${this.settings.sharpness}%</span>
        </div>
      </div>`;
  }

  /**
   * Phosphor section.
   *
   * This used to also carry an "NTSC Fringing" slider, which faked composite
   * artifacts by tinting detected edges. The core now demodulates the machine's
   * actual 14.31818 MHz dot stream, so fringing emerges from the signal where
   * it really belongs and a shader knob for it would only double-count.
   *
   * Colour bleed stays: it is vertical blending between scanlines, which models
   * phosphor spot overlap on the glass, not anything about the encoding.
   */
  _renderPhosphorSection() {
    return `
      <div class="settings-section">
        <div class="settings-section-title">Phosphor</div>
        <div class="setting-row">
          <label title="Vertical inter-scanline color blending (CRT phosphor overlap)">Color Bleed</label>
          <input type="range" id="ds-colorBleed" min="0" max="100" value="${this.settings.colorBleed}">
          <span class="setting-value" id="ds-val-colorBleed">${this.settings.colorBleed}%</span>
        </div>
      </div>`;
  }

  setupContentEventListeners() {
    // Set up slider listeners
    for (const section of this.sliderConfigs) {
      for (const slider of section.sliders) {
        const input = this.contentElement.querySelector(`#ds-${slider.id}`);
        const valueSpan = this.contentElement.querySelector(
          `#ds-val-${slider.id}`,
        );

        if (input) {
          input.addEventListener("input", (e) => {
            const value = parseInt(e.target.value, 10);
            this.settings[slider.id] = value;
            if (valueSpan) valueSpan.textContent = `${value}%`;
            this.applyToRenderer(slider.param, value / 100);
            this._markModified(slider.id);
            this.saveSettings();
          });
        }
      }
    }

    // Bezel color picker
    const colorPicker = this.contentElement.querySelector("#ds-bezelColor");
    if (colorPicker) {
      colorPicker.addEventListener("input", (e) => {
        this.settings.bezelColor = e.target.value;
        this.applyBezelColor(e.target.value);
        // No-op under a built-in preset, which does not own the bezel, but a
        // saved profile does — so changing it has to drop that profile's name.
        this._markModified("bezelColor");
        this.saveSettings();
      });
    }

    // Monitor preset
    const presetSelect = this.contentElement.querySelector("#ds-preset");
    if (presetSelect) {
      presetSelect.addEventListener("change", (e) => {
        this.applyPreset(e.target.value);
      });
    }

    // Advanced disclosure
    const advToggle = this.contentElement.querySelector("#ds-advanced-toggle");
    const advContent = this.contentElement.querySelector("#ds-advanced-content");
    if (advToggle && advContent) {
      advToggle.addEventListener("click", () => {
        const hidden = advContent.classList.toggle("hidden");
        advToggle.textContent = hidden ? "\u25B6 Advanced" : "\u25BC Advanced";
        this._fitHeightToContent();
      });
    }

    // Mask type dropdown
    const maskSelect = this.contentElement.querySelector("#ds-maskType");
    if (maskSelect) {
      maskSelect.addEventListener("change", (e) => {
        const value = parseInt(e.target.value, 10);
        this.settings.maskType = value;
        this.applyToRenderer("maskType", value);
        this._markModified("maskType");
        this.saveSettings();
      });
    }

    // Monochrome mode dropdown
    const monochromeSelect =
      this.contentElement.querySelector("#ds-monochromeMode");
    if (monochromeSelect) {
      monochromeSelect.addEventListener("change", (e) => {
        const value = parseInt(e.target.value, 10);
        this.settings.monochromeMode = value;
        // Tell emulator core to use monochrome rendering (bypasses NTSC artifacts)
        if (this.wasmModule && this.wasmModule._setMonochrome) {
          this.wasmModule._setMonochrome(value !== 0);
        }
        // Tell shader which phosphor color to use
        this.applyToRenderer("monochromeMode", value);
        this._markModified("monochromeMode");
        this.saveSettings();
      });
    }

    // Sharp pixels toggle
    const sharpToggle = this.contentElement.querySelector("#ds-sharpPixels");
    if (sharpToggle) {
      sharpToggle.addEventListener("change", (e) => {
        this.settings.sharpPixels = e.target.checked;
        if (this.renderer) {
          this.renderer.setNearestFilter(this.settings.sharpPixels);
        }
        this._markModified("sharpPixels");
        this.saveSettings();
      });
    }

    // Edge Sharpness slider (shader-based)
    const sharpnessInput = this.contentElement.querySelector("#ds-sharpness");
    const sharpnessValueSpan =
      this.contentElement.querySelector("#ds-val-sharpness");
    if (sharpnessInput) {
      sharpnessInput.addEventListener("input", (e) => {
        const value = parseInt(e.target.value, 10);
        this.settings.sharpness = value;
        if (sharpnessValueSpan) sharpnessValueSpan.textContent = `${value}%`;
        this.applyToRenderer("sharpness", value / 100);
        this._markModified("sharpness");
        this.saveSettings();
      });
    }

    // Color Bleed slider (shader-based)
    const colorBleedInput =
      this.contentElement.querySelector("#ds-colorBleed");
    const colorBleedValueSpan = this.contentElement.querySelector(
      "#ds-val-colorBleed",
    );
    if (colorBleedInput) {
      colorBleedInput.addEventListener("input", (e) => {
        const value = parseInt(e.target.value, 10);
        this.settings.colorBleed = value;
        if (colorBleedValueSpan)
          colorBleedValueSpan.textContent = `${value}%`;
        this.applyToRenderer("colorBleed", value / 100);
        this._markModified("colorBleed");
        this.saveSettings();
      });
    }


    // Profile actions
    const updateProfileBtn =
      this.contentElement.querySelector("#ds-profile-update");
    if (updateProfileBtn) {
      updateProfileBtn.addEventListener("click", () =>
        this.saveToSelectedProfile(),
      );
    }

    const saveAsProfileBtn =
      this.contentElement.querySelector("#ds-profile-saveas");
    if (saveAsProfileBtn) {
      saveAsProfileBtn.addEventListener("click", () =>
        this.saveCurrentAsProfile(),
      );
    }

    const deleteProfileBtn =
      this.contentElement.querySelector("#ds-profile-delete");
    if (deleteProfileBtn) {
      deleteProfileBtn.addEventListener("click", () =>
        this.deleteSelectedProfile(),
      );
    }

    // Reset button
    const resetBtn = this.contentElement.querySelector("#ds-reset");
    if (resetBtn) {
      resetBtn.addEventListener("click", () => this.resetToDefaults());
    }
  }

  create() {
    super.create();
    this.loadSettings();
    this.setupContentEventListeners();
    // applyAllSettings() is called by main.js after initialization
  }

  applyToRenderer(param, value) {
    if (this.renderer) {
      this.renderer.setParam(param, value);
    }
  }

  /**
   * Adopt a monitor preset, or "custom" to keep the current values.
   *
   * Selecting "custom" explicitly is a no-op on purpose: it is the label for
   * settings the user has already hand-tuned, so choosing it must not throw
   * that work away.
   */
  applyPreset(id) {
    this.settings.preset = id;
    // Whatever was selected before, its values have just been replaced, so
    // there are no longer edits pending against it.
    this.profileDirty = false;

    const profile = findProfile(this.userProfiles, id);
    const preset =
      profile || this.monitorPresets.find((p) => p.id === id) || null;

    if (preset) {
      Object.assign(this.settings, preset.values);
    }

    this.applyAllSettings();
    this.saveSettings();
  }

  /**
   * Note that a setting was changed by hand.
   *
   * Switching the label to "Custom" rather than leaving a preset selected
   * matters: a preset name that no longer describes what is on screen is worse
   * than no name at all. The values are left exactly as the user set them —
   * only the label changes.
   */
  _markModified(changedKey) {
    const values = this._selectedPresetValues();

    // A change that does not contradict the selection — brightness or bezel
    // under a built-in preset — leaves it intact, because the preset never
    // claimed to set those. A saved profile does claim to set everything, so
    // for a profile this check simply never exempts anything.
    if (values && !(changedKey in values)) return;

    // Editing a saved profile keeps it selected and marks it modified, rather
    // than dropping to Custom the way a built-in does. The difference is that
    // this profile can be saved over: losing its name here would leave nothing
    // for Save to write back to, and force a Save As for every small tweak.
    if (isProfileId(this.settings.preset)) {
      if (this.profileDirty) return;
      this.profileDirty = true;
      this._syncPresetUI();
      return;
    }

    if (this.settings.preset === "custom") return;

    this.settings.preset = "custom";
    this._syncPresetUI();
  }

  /**
   * Reclaim dead space when the window opens collapsed.
   */
  show() {
    super.show();
    requestAnimationFrame(() => {
      const advContent = this.contentElement?.querySelector("#ds-advanced-content");
      if (advContent && advContent.classList.contains("hidden")) {
        this._fitHeightToContent({ shrinkOnly: true });
      }
    });
  }

  /**
   * Grow or shrink the window to fit its content.
   *
   * Called when the disclosure toggles, so collapsing does not leave a tall
   * empty box and expanding does not bury the sliders behind a scrollbar.
   * On open it is shrink-only and only while collapsed: with the disclosure
   * shut there is nothing to scroll to, so trailing empty space is pure waste,
   * but growing the window would override a size the user chose themselves.
   */
  _fitHeightToContent({ shrinkOnly = false } = {}) {
    const inner = this.contentElement?.querySelector(".display-settings-content");
    if (!this.element || !inner) return;

    const headerHeight = this.headerElement ? this.headerElement.offsetHeight : 0;
    const style = getComputedStyle(this.contentElement);
    const padding =
      parseFloat(style.paddingTop || 0) + parseFloat(style.paddingBottom || 0);

    const desired = Math.ceil(inner.scrollHeight + headerHeight + padding);
    const available = window.innerHeight - this.currentY - 12;
    let height = Math.max(this.minHeight, Math.min(desired, available));
    if (shrinkOnly) height = Math.min(height, this.currentHeight);
    if (height === this.currentHeight) return;

    this.element.style.height = `${height}px`;
    this.currentHeight = height;

    if (this.onStateChange) this.onStateChange();
  }

  /**
   * Push the current preset id into the select and description line.
   */
  _syncPresetUI() {
    const select = this.contentElement?.querySelector("#ds-preset");
    if (select) {
      // Rebuilt rather than just re-selected, because saving or deleting a
      // profile changes which options exist.
      select.innerHTML = this._renderPresetOptions();
      select.value = this.settings.preset;
    }

    const description = this.contentElement?.querySelector("#ds-preset-description");
    // Never overwrite a confirmation that is still showing — the whole point of
    // it is that it survives the UI resync the save itself triggers.
    if (description && !this.statusTimer) {
      description.textContent = this._presetDescription();
    }

    // Save and Delete only mean something when a saved profile is selected,
    // and Save only when there is something to write. Disabling rather than
    // hiding keeps the button row from reflowing as the selection changes.
    const onProfile = isProfileId(this.settings.preset);

    const updateBtn = this.contentElement?.querySelector("#ds-profile-update");
    if (updateBtn) updateBtn.disabled = !onProfile || !this.profileDirty;

    const deleteBtn = this.contentElement?.querySelector("#ds-profile-delete");
    if (deleteBtn) deleteBtn.disabled = !onProfile;
  }

  /**
   * Briefly confirm an action in the description line.
   *
   * Saving is otherwise almost invisible: the values were already on screen
   * before the click, so without this the only feedback is a button greying
   * out, which reads as the button having stopped working rather than as the
   * save having happened.
   */
  _flashStatus(message) {
    const description = this.contentElement?.querySelector(
      "#ds-preset-description",
    );
    if (!description) return;

    clearTimeout(this.statusTimer);
    description.textContent = message;
    description.classList.add("status-confirm");

    this.statusTimer = setTimeout(() => {
      this.statusTimer = null;
      description.classList.remove("status-confirm");
      description.textContent = this._presetDescription();
    }, 1800);
  }

  /**
   * Write the current settings back to the selected profile.
   *
   * No prompt: the profile is already named and already selected, so there is
   * nothing to ask. This is the reason editing a profile keeps it selected
   * instead of falling back to Custom.
   */
  saveToSelectedProfile() {
    const profile = findProfile(this.userProfiles, this.settings.preset);
    if (!profile) return;

    const { profiles } = upsertProfile(
      this.userProfiles,
      profile.name,
      captureProfileValues(this.settings),
    );

    this.userProfiles = profiles;
    const stored = saveProfiles(this.userProfiles);
    this.profileDirty = false;
    this._syncPresetUI();

    if (stored) {
      this._flashStatus(`Saved to “${profile.name}”.`);
      showToast(`Display profile “${profile.name}” saved`, "info", 2500);
    } else {
      this._flashStatus("Could not save — storage is full.");
      showToast("Could not save the display profile", "error");
    }
  }

  /**
   * Save the current picture as a named profile.
   *
   * Offered as "Save As…" even when a profile is selected, because the name is
   * the identity: typing the same name back overwrites it, which is how you
   * update one, and typing a new name branches from it.
   */
  async saveCurrentAsProfile() {
    const suggested = findProfile(this.userProfiles, this.settings.preset)?.name ?? "";
    const entered = await showPrompt(
      "Save the current display settings as:",
      suggested,
      "Save",
    );
    if (entered === null) return;

    const check = validateProfileName(entered);
    if (!check.ok) {
      await showConfirm(check.error, "OK");
      return;
    }

    const { profiles, profile, replaced } = upsertProfile(
      this.userProfiles,
      check.name,
      captureProfileValues(this.settings),
    );

    if (replaced) {
      const proceed = await showConfirm(
        `A profile called "${profile.name}" already exists. Replace it?`,
        "Replace",
      );
      if (!proceed) return;
    }

    this.userProfiles = profiles;
    const stored = saveProfiles(this.userProfiles);

    // Select what was just saved, so the window stops saying Custom and the
    // name the user chose is the one on screen.
    this.settings.preset = profile.id;
    this.profileDirty = false;
    this._syncPresetUI();
    this.saveSettings();

    if (stored) {
      this._flashStatus(
        replaced ? `Replaced “${profile.name}”.` : `Saved as “${profile.name}”.`,
      );
      showToast(`Display profile “${profile.name}” saved`, "info", 2500);
    } else {
      this._flashStatus("Could not save — storage is full.");
      showToast("Could not save the display profile", "error");
    }
  }

  /**
   * Delete the selected profile.
   *
   * The picture is left exactly as it is and the selection falls back to
   * Custom. Deleting a profile is about forgetting a name, not about undoing
   * the settings the user is currently looking at.
   */
  async deleteSelectedProfile() {
    const profile = findProfile(this.userProfiles, this.settings.preset);
    if (!profile) return;

    const proceed = await showConfirm(
      `Delete the profile "${profile.name}"? The current picture will not change.`,
      "Delete",
    );
    if (!proceed) return;

    this.userProfiles = deleteProfile(this.userProfiles, profile.id);
    saveProfiles(this.userProfiles);

    this.settings.preset = "custom";
    this._syncPresetUI();
    this.saveSettings();
  }

  /**
   * Select the core's decoder.
   *
   * Monochrome is reached through the phosphor dropdown rather than here, so a
   * green-screen preset still remembers which colour decoder to return to.
   */
  applyColorMode() {
    const mode = this.settings.colorMode;
    if (mode === undefined || mode === COLOR_MODE.MONOCHROME) return;
    if (this.wasmModule && this.wasmModule._setVideoColorMode) {
      this.wasmModule._setVideoColorMode(mode);
    }
  }

  applyBezelColor(hex) {
    if (!this.renderer) return;
    const r = parseInt(hex.slice(1, 3), 16) / 255;
    const g = parseInt(hex.slice(3, 5), 16) / 255;
    const b = parseInt(hex.slice(5, 7), 16) / 255;
    this.renderer.setParam("surroundColor", [r, g, b]);
  }

  /** What a machine starts with: the shipped defaults, with its own border. */
  defaultsFor(profile) {
    return { ...this.defaults, overscan: defaultScreenBorder(profile) };
  }

  /**
   * The machine changed, and the settings are the machine's: load its own
   * and put them on. The saved profiles are not affected — they are named
   * snapshots any machine may pick.
   */
  onMachineChanged() {
    this.loadSettings();
    this.applyAllSettings();
  }

  applyAllSettings() {
    // Apply all slider values to renderer and update UI
    for (const section of this.sliderConfigs) {
      for (const slider of section.sliders) {
        const input = this.contentElement.querySelector(`#ds-${slider.id}`);
        const valueSpan = this.contentElement.querySelector(
          `#ds-val-${slider.id}`,
        );

        if (input) {
          input.value = this.settings[slider.id];
        }
        if (valueSpan) {
          valueSpan.textContent = `${this.settings[slider.id]}%`;
        }
        this.applyToRenderer(slider.param, this.settings[slider.id] / 100);
      }
    }

    // Apply bezel color
    const colorPicker = this.contentElement.querySelector("#ds-bezelColor");
    if (colorPicker) colorPicker.value = this.settings.bezelColor;
    this.applyBezelColor(this.settings.bezelColor);

    // Apply mask type
    const maskTypeSelect = this.contentElement.querySelector("#ds-maskType");
    if (maskTypeSelect) {
      maskTypeSelect.value = this.settings.maskType;
    }
    this.applyToRenderer("maskType", this.settings.maskType);

    // Tell the core which decoder to run over the dot stream. This must come
    // before the monochrome switch below: _setMonochrome(false) restores
    // whichever colour mode was last selected, so that mode has to be set first.
    this.applyColorMode();

    // Apply monochrome mode
    const monochromeSelect =
      this.contentElement.querySelector("#ds-monochromeMode");
    if (monochromeSelect) {
      monochromeSelect.value = this.settings.monochromeMode;
    }
    if (this.wasmModule && this.wasmModule._setMonochrome) {
      this.wasmModule._setMonochrome(this.settings.monochromeMode !== 0);
    }
    // Tell shader which phosphor color to use
    this.applyToRenderer("monochromeMode", this.settings.monochromeMode);

    // Apply sharp pixels
    const sharpToggle = this.contentElement.querySelector("#ds-sharpPixels");
    if (sharpToggle) {
      sharpToggle.checked = this.settings.sharpPixels;
    }
    if (this.renderer) {
      this.renderer.setNearestFilter(this.settings.sharpPixels);
    }

    // Apply edge sharpness (shader-based)
    {
      const input = this.contentElement.querySelector("#ds-sharpness");
      const valueSpan = this.contentElement.querySelector("#ds-val-sharpness");
      const value = this.settings.sharpness ?? 0;
      if (input) input.value = value;
      if (valueSpan) valueSpan.textContent = `${value}%`;
      this.applyToRenderer("sharpness", value / 100);
    }

    // Apply color bleed settings (shader-based)
    {
      const input = this.contentElement.querySelector("#ds-colorBleed");
      const valueSpan = this.contentElement.querySelector("#ds-val-colorBleed");
      if (input) input.value = this.settings.colorBleed;
      if (valueSpan)
        valueSpan.textContent = `${this.settings.colorBleed}%`;
      this.applyToRenderer("colorBleed", this.settings.colorBleed / 100);
    }


    this._syncPresetUI();
  }

  resetToDefaults() {
    this.settings = this.defaultsFor(getMachineProfile());
    this.profileDirty = false;
    this.applyAllSettings();
    this.saveSettings();
  }

  saveSettings() {
    try {
      localStorage.setItem(
        displaySettingsKey(getMachineProfile()),
        JSON.stringify({ ...this.settings, profileDirty: this.profileDirty }),
      );
    } catch (e) {
      console.warn("Could not save display settings:", e);
    }
  }

  /**
   * Load the running machine's settings, or start it from its defaults.
   *
   * A machine with nothing saved gets its defaults rather than another
   * machine's settings; the exception is the //e, which takes the settings
   * saved before there was more than one machine, since those were its.
   */
  loadSettings() {
    const machine = getMachineProfile();
    this.settings = this.defaultsFor(machine);
    this.profileDirty = false;
    try {
      let saved = localStorage.getItem(displaySettingsKey(machine));
      if (!saved && inheritsLegacySettings(machine)) {
        saved = localStorage.getItem(LEGACY_DISPLAY_SETTINGS_KEY);
      }
      if (saved) {
        const parsed = JSON.parse(saved);
        this.settings = { ...this.defaultsFor(machine), ...parsed };

        // Settings saved before the core gained a real NTSC decoder have no
        // colorMode. Recover it from the preset they had selected, so someone
        // who left on Composite does not silently come back on Pixel Exact.
        if (parsed.colorMode === undefined) {
          const preset = this.monitorPresets.find(
            (p) => p.id === this.settings.preset,
          );
          this.settings.colorMode = preset
            ? preset.values.colorMode
            : this.defaults.colorMode;
        }

        // Dead key from the shader-based fringing this replaced.
        delete this.settings.ntscFringing;

        // A profile selected last session may since have been deleted — from
        // another tab, or by clearing storage. The picture is still whatever
        // was saved, so keep it and just stop claiming a name for it.
        if (
          isProfileId(this.settings.preset) &&
          !findProfile(this.userProfiles, this.settings.preset)
        ) {
          this.settings.preset = "custom";
        }

        // Unsaved edits to a profile have to survive a reload, or Save comes
        // back disabled and there is no way to commit them. It rides along in
        // the settings blob but is not a setting, so it does not stay in
        // `settings` — otherwise it would end up captured inside a profile.
        this.profileDirty =
          isProfileId(this.settings.preset) && Boolean(parsed.profileDirty);
        delete this.settings.profileDirty;

        // Re-adopt a built-in preset's current values.
        //
        // Editing anything a built-in owns switches the selection to Custom, so
        // still being on a built-in means these values came from the preset and
        // were never touched. Taking them fresh is therefore lossless, and it
        // means a change to a preset definition reaches people who already had
        // it selected instead of only new users. Calibration and bezel are left
        // alone, because a preset does not own those.
        if (!isProfileId(this.settings.preset)) {
          const preset = this.monitorPresets.find(
            (p) => p.id === this.settings.preset,
          );
          if (preset) Object.assign(this.settings, preset.values);
        }
      }
    } catch (e) {
      console.warn("Could not load display settings:", e);
    }
  }

  update() {
    // No dynamic updates needed for display settings
  }
}
