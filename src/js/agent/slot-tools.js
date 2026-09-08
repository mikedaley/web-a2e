/*
 * slot-tools.js - Expansion slot management tools
 *
 * Written by
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

import { saveSlotConfig } from "../machine/slot-storage.js";

// Slot metadata (matches slot-configuration-window.js)
const SLOT_CONFIG = [
  { slot: 1, compatible: ["parallel", "ssc", "softcard"], fixed: false },
  { slot: 2, compatible: ["parallel", "ssc", "smartport", "softcard"], fixed: false },
  { slot: 3, compatible: [], fixed: true },
  { slot: 4, compatible: ["mockingboard", "mouse", "smartport", "softcard"], fixed: false },
  { slot: 5, compatible: ["thunderclock", "smartport", "softcard"], fixed: false },
  { slot: 6, compatible: ["disk2"], fixed: false },
  { slot: 7, compatible: ["thunderclock", "smartport", "softcard"], fixed: false },
];

const ALL_CARDS = ["disk2", "mockingboard", "thunderclock", "mouse", "smartport", "softcard", "ssc", "parallel"];

function getWasm() {
  const wasmModule = window.emulator?.wasmModule;
  if (!wasmModule) {
    throw new Error("WASM module not available");
  }
  return wasmModule;
}

async function getSlotCard(wasmModule, slot) {
  const ptr = await wasmModule._getSlotCard(slot);
  return ptr ? await wasmModule.UTF8ToString(ptr) : "empty";
}

function getSlotConfig(slot) {
  return SLOT_CONFIG.find(cfg => cfg.slot === slot);
}

async function setSlotCardWasm(wasmModule, slot, cardId) {
  const cardIdPtr = await wasmModule._malloc(cardId.length + 1);
  await wasmModule.stringToUTF8(cardId, cardIdPtr, cardId.length + 1);
  wasmModule._setSlotCard(slot, cardIdPtr);
  await wasmModule._free(cardIdPtr);
}

async function persistSlotConfig(wasmModule) {
  const config = {};
  try {
    for (const cfg of SLOT_CONFIG) {
      if (!cfg.fixed) {
        config[cfg.slot] = await getSlotCard(wasmModule, cfg.slot);
      }
    }
    saveSlotConfig(config);
  } catch (e) {
    // Non-fatal
  }

  // Refresh the slot configuration window UI if it exists
  const slotWindow = window.emulator?.windowManager?.getWindow("slot-configuration");
  if (slotWindow) {
    if (typeof slotWindow.loadSettings === "function") slotWindow.loadSettings();
    if (typeof slotWindow.updateDisabledOptions === "function") slotWindow.updateDisabledOptions();
  }

  // Refresh the printer interface gate. The slot-window UI path notifies the
  // printer via onSlotsApplied (main.js); the agent path must do it too, or a
  // card installed via slotsInstallCard leaves the printer's bus availability
  // (DMP/FX-80 over parallel, IW-I/II over SSC) stale until a full page reload.
  const printerManager = window.emulator?.printerManager;
  if (printerManager && typeof printerManager.updateSlots === "function") {
    printerManager.updateSlots(config);
  }
}

export const slotTools = {
  /**
   * List all expansion slots with current card and available options
   */
  slotsListAll: async () => {
    const wasmModule = getWasm();

    // Get current card in every slot
    const slotState = [];
    for (const cfg of SLOT_CONFIG) {
      slotState.push({
        slot: cfg.slot,
        currentCard: cfg.fixed ? "80col" : await getSlotCard(wasmModule, cfg.slot),
        fixed: cfg.fixed,
        compatible: cfg.compatible,
      });
    }

    // Collect all cards currently installed
    const installedCards = {};
    for (const s of slotState) {
      if (!s.fixed && s.currentCard !== "empty") {
        installedCards[s.currentCard] = s.slot;
      }
    }

    // For each slot, determine which compatible cards are available to install
    const slots = slotState.map(s => {
      const available = s.fixed
        ? []
        : s.compatible.filter(card => {
          return !installedCards[card] || installedCards[card] === s.slot;
        });

      return {
        slot: s.slot,
        currentCard: s.currentCard,
        fixed: s.fixed,
        compatible: s.compatible,
        available,
      };
    });

    const uninstalled = ALL_CARDS.filter(card => !installedCards[card]);

    return {
      success: true,
      slots,
      installedCards,
      uninstalledCards: uninstalled,
      message: `${Object.keys(installedCards).length} card(s) installed, ${uninstalled.length} available`,
    };
  },

  /**
   * Install a card into a slot (auto-removes from previous slot if needed)
   */
  slotsInstallCard: async (args) => {
    const { slot, card } = args;

    if (slot === undefined || slot === null) {
      throw new Error("slot parameter is required");
    }
    if (!card) {
      throw new Error("card parameter is required");
    }
    if (!ALL_CARDS.includes(card)) {
      throw new Error(`Unknown card: ${card}. Valid cards: ${ALL_CARDS.join(", ")}`);
    }

    const cfg = getSlotConfig(slot);
    if (!cfg) {
      throw new Error(`Invalid slot: ${slot}. Valid slots: 1-7`);
    }
    if (cfg.fixed) {
      throw new Error(`Slot ${slot} is fixed and cannot be changed`);
    }
    if (!cfg.compatible.includes(card)) {
      throw new Error(
        `${card} is not compatible with slot ${slot}. Compatible: ${cfg.compatible.join(", ") || "none"}`
      );
    }

    const wasmModule = getWasm();
    const currentCard = await getSlotCard(wasmModule, slot);

    // Already installed in this slot
    if (currentCard === card) {
      return {
        success: true,
        slot,
        card,
        message: `${card} is already installed in slot ${slot}`,
        reset: false,
      };
    }

    // If the card is installed in another slot, remove it from there
    let movedFrom = null;
    for (const c of SLOT_CONFIG) {
      if (!c.fixed && c.slot !== slot && await getSlotCard(wasmModule, c.slot) === card) {
        await setSlotCardWasm(wasmModule, c.slot, "empty");
        movedFrom = c.slot;
        break;
      }
    }

    // Install the card
    await setSlotCardWasm(wasmModule, slot, card);
    await persistSlotConfig(wasmModule);
    wasmModule._reset();
    // Slot config drives mouse-capture enable; re-arm it like the Slot Config
    // UI does, or ⌥-click stays gated off after an agent install/remove/move.
    try { await window.emulator?.updateMouseHandlerState?.(); } catch (e) { /* non-fatal */ }

    const displaced = currentCard !== "empty" ? currentCard : null;
    let message = `${card} installed in slot ${slot}. Emulator reset.`;
    if (movedFrom) {
      message = `${card} moved from slot ${movedFrom} to slot ${slot}. Emulator reset.`;
    }
    if (displaced && displaced !== card) {
      message += ` Displaced ${displaced}.`;
    }

    return {
      success: true,
      slot,
      card,
      displaced,
      movedFrom,
      message,
      reset: true,
    };
  },

  /**
   * Remove a card from a slot (set to empty)
   */
  slotsRemoveCard: async (args) => {
    const { slot } = args;

    if (slot === undefined || slot === null) {
      throw new Error("slot parameter is required");
    }

    const cfg = getSlotConfig(slot);
    if (!cfg) {
      throw new Error(`Invalid slot: ${slot}. Valid slots: 1-7`);
    }
    if (cfg.fixed) {
      throw new Error(`Slot ${slot} is fixed and cannot be changed`);
    }

    const wasmModule = getWasm();
    const currentCard = await getSlotCard(wasmModule, slot);

    if (currentCard === "empty") {
      return {
        success: true,
        slot,
        message: `Slot ${slot} is already empty`,
        reset: false,
      };
    }

    await setSlotCardWasm(wasmModule, slot, "empty");
    await persistSlotConfig(wasmModule);
    wasmModule._reset();
    // Slot config drives mouse-capture enable; re-arm it like the Slot Config
    // UI does, or ⌥-click stays gated off after an agent install/remove/move.
    try { await window.emulator?.updateMouseHandlerState?.(); } catch (e) { /* non-fatal */ }

    return {
      success: true,
      slot,
      removed: currentCard,
      message: `Removed ${currentCard} from slot ${slot}. Emulator reset.`,
      reset: true,
    };
  },

  /**
   * Move a card from one slot to another
   */
  slotsMoveCard: async (args) => {
    const { fromSlot, toSlot } = args;

    if (fromSlot === undefined || fromSlot === null) {
      throw new Error("fromSlot parameter is required");
    }
    if (toSlot === undefined || toSlot === null) {
      throw new Error("toSlot parameter is required");
    }
    if (fromSlot === toSlot) {
      throw new Error("fromSlot and toSlot must be different");
    }

    const fromCfg = getSlotConfig(fromSlot);
    const toCfg = getSlotConfig(toSlot);
    if (!fromCfg) {
      throw new Error(`Invalid fromSlot: ${fromSlot}. Valid slots: 1-7`);
    }
    if (!toCfg) {
      throw new Error(`Invalid toSlot: ${toSlot}. Valid slots: 1-7`);
    }
    if (fromCfg.fixed) {
      throw new Error(`Slot ${fromSlot} is fixed and cannot be changed`);
    }
    if (toCfg.fixed) {
      throw new Error(`Slot ${toSlot} is fixed and cannot be changed`);
    }

    const wasmModule = getWasm();
    const card = await getSlotCard(wasmModule, fromSlot);

    if (card === "empty") {
      throw new Error(`Slot ${fromSlot} is empty — nothing to move`);
    }

    if (!toCfg.compatible.includes(card)) {
      throw new Error(
        `${card} is not compatible with slot ${toSlot}. Compatible: ${toCfg.compatible.join(", ") || "none"}`
      );
    }

    const occupant = await getSlotCard(wasmModule, toSlot);
    if (occupant !== "empty") {
      throw new Error(
        `Slot ${toSlot} is occupied by ${occupant}. Remove it first with slotsRemoveCard.`
      );
    }

    await setSlotCardWasm(wasmModule, fromSlot, "empty");
    await setSlotCardWasm(wasmModule, toSlot, card);
    await persistSlotConfig(wasmModule);
    wasmModule._reset();
    // Slot config drives mouse-capture enable; re-arm it like the Slot Config
    // UI does, or ⌥-click stays gated off after an agent install/remove/move.
    try { await window.emulator?.updateMouseHandlerState?.(); } catch (e) { /* non-fatal */ }

    return {
      success: true,
      card,
      fromSlot,
      toSlot,
      message: `Moved ${card} from slot ${fromSlot} to slot ${toSlot}. Emulator reset.`,
      reset: true,
    };
  },
};
