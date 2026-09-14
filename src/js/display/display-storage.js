/*
 * display-storage.js - Where a machine's display settings are kept
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/*
 * Display settings are remembered per machine. The machines do not want the
 * same picture: a //e's fills its frame edge to edge and looks like a screen
 * only with a strip of blank glass round it, while a IIgs draws its own
 * border in the colour the Control Panel chose and a second one round that
 * would be a frame round a frame. Someone who tunes a composite look for
 * games on a //e should not find it on a IIgs's RGB desktop, and the other
 * way about.
 *
 * So each machine has its own key. The key everything used before there was
 * more than one machine holds a //e's settings, and is read as that
 * machine's — once, on the way to its own key — and then left alone.
 */

const STORAGE_PREFIX = "a2e-display-settings";

/** The key that held every machine's settings before they were kept apart. */
export const LEGACY_DISPLAY_SETTINGS_KEY = STORAGE_PREFIX;
const LEGACY_MACHINE = "apple2e";

export function displaySettingsKey(profile) {
  const key = profile?.key || LEGACY_MACHINE;
  return `${STORAGE_PREFIX}:${key}`;
}

/** Whether a machine may take the pre-machine settings as its own. */
export function inheritsLegacySettings(profile) {
  return (profile?.key || LEGACY_MACHINE) === LEGACY_MACHINE;
}

/**
 * The "Screen Border" slider's starting value, on its 0-100 scale: a strip
 * of blank glass round the 8-bit machines' picture, and none round a IIgs's,
 * which has a border of its own.
 */
export const DEFAULT_SCREEN_BORDER = 35;

export function defaultScreenBorder(profile) {
  return profile?.family === "apple2gs" ? 0 : DEFAULT_SCREEN_BORDER;
}
