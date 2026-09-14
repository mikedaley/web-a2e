/*
 * machine-availability.js - Which menu items the running machine can use
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/*
 * A menu item that opens a window for hardware the machine does not have is
 * not a feature, it is a dead end: a Mockingboard window on a //c, which has
 * nowhere to fit one; an Expansion Slots window on a //c, whose every slot is
 * soldered down; a CPU speed selector on a IIgs, whose core has no
 * multiplier. Each answer here is a fact about the machine or about what is
 * fitted to it, and the menus hide what the answer rules out rather than
 * offering it and letting it fail.
 */

const SERIAL_CARDS = new Set(["ssc", "serial1", "serial2"]);
const PRINTER_CARDS = new Set(["parallel", "ssc", "serial1", "serial2"]);

function fitted(cards, wanted) {
  return Object.values(cards || {}).some((id) =>
    typeof wanted === "string" ? id === wanted : wanted.has(id),
  );
}

/**
 * What the menus may offer for this machine with these cards fitted.
 *
 * @param {object} profile - the machine's profile, as `getMachineProfile()` gives it
 * @param {object} cards - slot number → card id, fixed slots included
 * @returns {{slots:boolean, speed:boolean, hardDrives:boolean,
 *            serialPort:boolean, printer:boolean, mockingboard:boolean,
 *            mouseCard:boolean, basic:boolean, assembler:boolean}}
 */
export function menuAvailability(profile, cards) {
  const iigs = profile?.family === "apple2gs";
  const caps = profile?.caps || {};
  return {
    // The window changes cards through `_setSlotCard`, which a IIgs's core
    // does not answer: its slots are "the card or the port" and that third
    // state is not modelled yet. A //c has no sockets at all.
    slots: caps.hasExpansionSlots !== false && !iigs,
    // The multiplier lives in Emulator, which a IIgs is not built from.
    speed: !iigs,
    // A IIgs has a SmartPort in slot 5 as part of the machine.
    hardDrives: iigs || fitted(cards, "smartport"),
    serialPort: fitted(cards, SERIAL_CARDS),
    printer: fitted(cards, PRINTER_CARDS),
    mockingboard: fitted(cards, "mockingboard"),
    // The window shows a card's PIA. A //c's "mouse" in slot 4 is the IOU,
    // which has no PIA to show and is not a card.
    mouseCard: caps.hasExpansionSlots !== false && fitted(cards, "mouse"),
    // The BASIC and assembler tools were written against a //e's memory and
    // ROM entry points and have not been brought to a IIgs yet.
    basic: !iigs,
    assembler: !iigs,
  };
}
