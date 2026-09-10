/*
 * game-port.js - What is plugged into the game I/O connector
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * The Apple II's game connector took a resistive joystick: two potentiometers
 * the machine times an RC discharge against, plus two pushbuttons. Sirius
 * Software's Joyport put two Atari CX40-style digital sticks on the same
 * connector instead, reading them through the annunciators and the three
 * pushbutton inputs.
 *
 * The two cannot coexist — the Joyport drives PB0/PB1 itself, and drives them
 * inverted — so this is a choice, exactly as it was on the desk. The core
 * holds the same choice as a `GamePortDevice`; this module owns the host side
 * of it: which one is selected, how it is remembered, and how a browser
 * gamepad's axes and buttons become the five switches of a digital stick.
 */

const STORAGE_KEY = "a2e-game-port";

export const GAME_PORT_APPLE = "apple";
export const GAME_PORT_JOYPORT = "joyport";

export const DEFAULT_GAME_PORT = GAME_PORT_APPLE;

export const GAME_PORT_DEVICES = [
  {
    id: GAME_PORT_APPLE,
    label: "Apple Joystick",
    description: "Analog paddles, PDL0-3 and two buttons",
  },
  {
    id: GAME_PORT_JOYPORT,
    label: "Sirius Joyport",
    description: "Two Atari-style digital sticks",
  },
];

/** The integer the core's `_setGamePortDevice` expects. */
export const GAME_PORT_CODE = {
  [GAME_PORT_APPLE]: 0,
  [GAME_PORT_JOYPORT]: 1,
};

/** Switch mask bits, matching `Joyport::SwitchBit` in the core. */
export const SWITCH = {
  UP: 1 << 0,
  DOWN: 1 << 1,
  LEFT: 1 << 2,
  RIGHT: 1 << 3,
  FIRE: 1 << 4,
};

/** The Joyport takes two sticks and no more. */
export const JOYPORT_STICKS = 2;

/**
 * How far an analog stick must be pushed before the digital switch closes.
 * A CX40's switches sit under the very first millimetres of travel, but a
 * modern thumbstick rests noisily near centre, so this is deliberately well
 * clear of the gamepad deadzone rather than at it.
 */
export const DIGITAL_THRESHOLD = 0.5;

export function isGamePortDevice(value) {
  return value === GAME_PORT_APPLE || value === GAME_PORT_JOYPORT;
}

export function loadStoredGamePort() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return isGamePortDevice(raw) ? raw : DEFAULT_GAME_PORT;
  } catch {
    return DEFAULT_GAME_PORT;
  }
}

export function storeGamePort(device) {
  try {
    localStorage.setItem(
      STORAGE_KEY,
      isGamePortDevice(device) ? device : DEFAULT_GAME_PORT,
    );
  } catch {
    /* private browsing - the choice still applies for this session */
  }
}

/**
 * Build a switch mask from four booleans and a fire button.
 *
 * A real stick cannot close left and right at once — the gate will not let it
 * — so an impossible pair is dropped rather than sent on. Games read the two
 * halves of an axis pair as independent lines and a program that saw both
 * would take whichever it tested first, which is a coin toss the hardware
 * never asked anyone to make.
 */
export function switchesFromDirections({
  up = false,
  down = false,
  left = false,
  right = false,
  fire = false,
} = {}) {
  let mask = 0;
  if (up && !down) mask |= SWITCH.UP;
  if (down && !up) mask |= SWITCH.DOWN;
  if (left && !right) mask |= SWITCH.LEFT;
  if (right && !left) mask |= SWITCH.RIGHT;
  if (fire) mask |= SWITCH.FIRE;
  return mask;
}

/**
 * Build a switch mask from an analog stick position.
 *
 * @param {number} x  -1 (left) .. 1 (right)
 * @param {number} y  -1 (up) .. 1 (down)
 * @param {boolean} fire
 * @param {number} threshold
 */
export function switchesFromAxes(x, y, fire = false, threshold = DIGITAL_THRESHOLD) {
  return switchesFromDirections({
    left: x <= -threshold,
    right: x >= threshold,
    up: y <= -threshold,
    down: y >= threshold,
    fire,
  });
}

/** Human-readable switch list, for the Joyport window's readout. */
export function describeSwitches(mask) {
  const names = [];
  if (mask & SWITCH.UP) names.push("Up");
  if (mask & SWITCH.DOWN) names.push("Down");
  if (mask & SWITCH.LEFT) names.push("Left");
  if (mask & SWITCH.RIGHT) names.push("Right");
  if (mask & SWITCH.FIRE) names.push("Fire");
  return names.length ? names.join(" + ") : "Centred";
}
