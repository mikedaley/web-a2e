/*
 * slot-storage.js - Remembering which cards are in which slots, per machine
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { getMachineProfile } from "./machine-profile.js";

/*
 * Slot configuration is per machine, and has to be.
 *
 * The machines do not agree about what a slot even is: a //e has slots 1-7
 * with an 80-column card bolted into slot 3, while a II+ has 0-7 with its
 * language card in slot 0 and slot 3 free. Storing one configuration for both
 * meant a card the user put in a II+'s slot 3 reappeared as a //e's built-in
 * 80-column card, and a //e's SmartPort followed them onto a machine whose
 * defaults are a bare Disk II.
 *
 * So each machine gets its own key. There is deliberately no shared default
 * underneath them: what a fresh machine ships with is in its profile.
 */
const KEY_PREFIX = "a2e-slot-config";

/*
 * The key everything used before there was more than one machine. It holds a
 * //e configuration, because a //e was the only thing that could have written
 * it, so it is read once as the //e's starting point and then superseded.
 *
 * It is left in place rather than deleted. The migration has already copied
 * what it holds, so an orphan costs nothing, and losing somebody's slot layout
 * to a mistake in that copy would cost rather more.
 */
const LEGACY_KEY = "a2e-slot-config";
const LEGACY_MACHINE = "apple2e";

function storageKey(machineKey) {
  return `${KEY_PREFIX}:${machineKey}`;
}

/** The cards a fresh example of this machine ships with, from its profile. */
export function defaultSlotConfig(machine = getMachineProfile()) {
  const config = {};
  for (const slot of machine.slots || []) {
    if (slot.fixedCard) continue; // Not the user's to change
    if (slot.defaultCard) config[slot.slot] = slot.defaultCard;
  }
  return config;
}

/**
 * The saved slot configuration for a machine, or null if it has never been
 * saved. Callers fall back to defaultSlotConfig() so that "never configured"
 * and "deliberately emptied" stay distinguishable.
 */
export function loadSlotConfig(machine = getMachineProfile()) {
  try {
    const saved = localStorage.getItem(storageKey(machine.key));
    if (saved) return JSON.parse(saved);

    // First run since slot configuration became per-machine: adopt whatever
    // the single shared key holds, which can only have come from a //e.
    if (machine.key === LEGACY_MACHINE) {
      const legacy = localStorage.getItem(LEGACY_KEY);
      if (legacy) {
        const config = JSON.parse(legacy);
        saveSlotConfig(config, machine);
        return config;
      }
    }
  } catch (e) {
    console.warn("Could not read the slot configuration:", e);
  }
  return null;
}

/** The saved configuration, or the machine's defaults when there is none. */
export function slotConfigOrDefaults(machine = getMachineProfile()) {
  return loadSlotConfig(machine) || defaultSlotConfig(machine);
}

/** Remember a slot configuration for a machine. */
export function saveSlotConfig(config, machine = getMachineProfile()) {
  try {
    localStorage.setItem(storageKey(machine.key), JSON.stringify(config));
  } catch (e) {
    console.warn("Could not save the slot configuration:", e);
  }
}
