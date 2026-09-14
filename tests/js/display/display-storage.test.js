/*
 * display-storage.test.js - Display settings are the machine's
 */

import { describe, expect, it } from "vitest";

import {
  DEFAULT_SCREEN_BORDER,
  LEGACY_DISPLAY_SETTINGS_KEY,
  defaultScreenBorder,
  displaySettingsKey,
  inheritsLegacySettings,
} from "../../../src/js/display/display-storage.js";

const IIE = { key: "apple2e", family: "apple2" };
const IIC = { key: "apple2c", family: "apple2" };
const IIGS = { key: "apple2gs", family: "apple2gs" };

describe("display storage", () => {
  it("keeps each machine's settings under its own key", () => {
    expect(displaySettingsKey(IIE)).not.toBe(displaySettingsKey(IIGS));
    expect(displaySettingsKey(IIE)).not.toBe(LEGACY_DISPLAY_SETTINGS_KEY);
    expect(displaySettingsKey(IIGS)).toBe("a2e-display-settings:apple2gs");
  });

  it("lets only the //e inherit the settings from before there were machines", () => {
    expect(inheritsLegacySettings(IIE)).toBe(true);
    expect(inheritsLegacySettings(IIC)).toBe(false);
    expect(inheritsLegacySettings(IIGS)).toBe(false);
    expect(inheritsLegacySettings(undefined)).toBe(true);
  });

  it("gives the 8-bit machines a border and the IIgs none, since it draws its own", () => {
    expect(defaultScreenBorder(IIE)).toBe(35);
    expect(defaultScreenBorder(IIC)).toBe(35);
    expect(defaultScreenBorder(IIGS)).toBe(0);
    expect(DEFAULT_SCREEN_BORDER).toBe(35);
  });
});
