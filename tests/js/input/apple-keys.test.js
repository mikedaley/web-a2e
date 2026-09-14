/*
 * apple-keys.test.js - Which host key is Open Apple is a per-machine choice
 */

import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  commandKeyIsOpenApple,
  defaultCommandKeyIsOpenApple,
  setCommandKeyIsOpenApple,
} from "../../../src/js/input/apple-keys.js";

function fakeStorage(initial = {}) {
  const data = { ...initial };
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = String(v);
    },
    removeItem: (k) => {
      delete data[k];
    },
  };
}

const IIE = { key: "apple2e", family: "apple2" };
const IIGS = { key: "apple2gs", family: "apple2gs" };

beforeEach(() => {
  globalThis.localStorage = fakeStorage();
});

afterEach(() => {
  delete globalThis.localStorage;
});

describe("apple keys", () => {
  it("gives a IIgs the ⌘ key and leaves a //e's to the browser", () => {
    expect(defaultCommandKeyIsOpenApple(IIGS)).toBe(true);
    expect(defaultCommandKeyIsOpenApple(IIE)).toBe(false);
    expect(commandKeyIsOpenApple(IIGS)).toBe(true);
    expect(commandKeyIsOpenApple(IIE)).toBe(false);
  });

  it("remembers a choice for one machine without touching another", () => {
    setCommandKeyIsOpenApple(false, IIGS);
    expect(commandKeyIsOpenApple(IIGS)).toBe(false);
    expect(commandKeyIsOpenApple(IIE)).toBe(false);
    setCommandKeyIsOpenApple(true, IIE);
    expect(commandKeyIsOpenApple(IIE)).toBe(true);
    expect(commandKeyIsOpenApple(IIGS)).toBe(false);
  });

  it("ignores a value it did not write", () => {
    globalThis.localStorage = fakeStorage({
      "a2e-apple-keys:apple2gs": "banana",
    });
    expect(commandKeyIsOpenApple(IIGS)).toBe(true);
  });

  it("answers without storage at all", () => {
    delete globalThis.localStorage;
    expect(commandKeyIsOpenApple(IIGS)).toBe(true);
    expect(() => setCommandKeyIsOpenApple(false, IIGS)).not.toThrow();
  });
});
