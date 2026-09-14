/*
 * apple-keys.js - Which host keys are the Apple keys
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { getMachineProfile } from "../machine/machine-profile.js";

/*
 * A //e has Open Apple and Closed Apple either side of the space bar, and on a
 * Mac the natural stand-ins are the two Option keys: left is Open, right is
 * Closed, and ⌘ is left to the browser.
 *
 * A IIgs's keyboard is a Mac's: the key marked ⌘ *is* the Open Apple key and
 * the one marked Option is the Closed Apple. GS/OS and every desktop program
 * on it use ⌘-letter for their menus, so on that machine the emulator takes
 * ⌘ for itself while it has the keyboard, and Option — either side — becomes
 * Closed Apple. That is a choice rather than a rule, because a browser holds
 * some ⌘ shortcuts back for itself whatever a page asks, and somebody may
 * prefer to keep them; it is remembered per machine, since its right default
 * depends on which machine it is.
 */

const STORAGE_PREFIX = "a2e-apple-keys:";

function storageKey(machine) {
  return `${STORAGE_PREFIX}${machine.key}`;
}

/** What the machine would choose for itself. */
export function defaultCommandKeyIsOpenApple(machine = getMachineProfile()) {
  return machine?.family === "apple2gs";
}

/** Whether ⌘ is the Open Apple key on this machine. */
export function commandKeyIsOpenApple(machine = getMachineProfile()) {
  try {
    const saved = localStorage.getItem(storageKey(machine));
    if (saved === "command") return true;
    if (saved === "option") return false;
  } catch {
    // Storage may be unavailable; the default still answers.
  }
  return defaultCommandKeyIsOpenApple(machine);
}

export function setCommandKeyIsOpenApple(
  enabled,
  machine = getMachineProfile(),
) {
  try {
    localStorage.setItem(storageKey(machine), enabled ? "command" : "option");
  } catch {
    // Nothing to remember it in; the session keeps the choice until reload.
  }
}
