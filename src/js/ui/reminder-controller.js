/*
 * reminder-controller.js - UI reminder notifications
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/** The user's "Don't show again" for the mouse capture reminder. */
const MOUSE_REMINDER_DISMISSED_KEY = "a2e-mouse-reminder-dismissed";

/** How far above the bottom of the picture the mouse reminder sits. */
const MOUSE_REMINDER_INSET_PX = 24;

/**
 * ReminderController - Manages floating reminder tooltips
 * Handles power, resize, and drives toggle reminders with positioning and persistence
 */

export class ReminderController {
  constructor() {
    this.isPowerReminderVisible = false;
    this.isBasicReminderVisible = false;
    this.isMouseReminderVisible = false;
    this._followHandle = 0;

    this._nameTheCaptureKey();

    const dismiss = document.getElementById("btn-mouse-reminder-dismiss");
    if (dismiss) {
      dismiss.addEventListener("click", () => this.dismissMouseReminder());
    }

    window.addEventListener("resize", () => this.repositionAll());
  }

  /**
   * Position a reminder tooltip below a target element with an arrow pointing to it.
   * Centers the reminder on the element, clamps to viewport, and sets arrow position.
   * @param {string} reminderId - The reminder element's ID
   * @param {string|Element} target - The target element ID or element to position below
   * @param {number} defaultWidth - Fallback width if reminder not yet rendered
   */
  positionReminderBelowElement(reminderId, target) {
    const reminder = document.getElementById(reminderId);
    const targetEl =
      typeof target === "string" ? document.getElementById(target) : target;
    if (!reminder || !targetEl) return;

    const targetRect = targetEl.getBoundingClientRect();
    const targetCenterX = targetRect.left + targetRect.width / 2;
    const reminderWidth = reminder.offsetWidth;

    // Position reminder centered below target, clamped to viewport
    let reminderLeft = targetCenterX - reminderWidth / 2;
    const padding = 16;
    const maxLeft = window.innerWidth - reminderWidth - padding;
    reminderLeft = Math.max(padding, Math.min(reminderLeft, maxLeft));

    // Calculate arrow position relative to reminder
    const arrowLeft = targetCenterX - reminderLeft;

    reminder.style.left = `${reminderLeft}px`;
    reminder.style.top = `${targetRect.bottom + 15}px`;
    reminder.style.setProperty("--arrow-left", `${arrowLeft}px`);
  }

  /**
   * Show a reminder, measure it off-screen, then position it correctly.
   * Uses a delayed reposition to handle layout settling during app init.
   */
  _showReminder(reminder, repositionFn, flagName) {
    this[flagName] = true;
    // Render off-screen so the browser can compute its real size
    reminder.style.left = '-9999px';
    reminder.style.top = '-9999px';
    reminder.classList.remove("hidden");
    // Delay positioning to let the full page layout settle after init
    setTimeout(() => {
      if (this[flagName]) repositionFn();
    }, 100);
  }

  // Power reminder methods

  /** The reminder says which machine the button starts. */
  setMachineName(name) {
    const el = document.getElementById("power-reminder-machine");
    if (el && name) el.textContent = name;
  }

  repositionPowerReminder() {
    this.positionReminderBelowElement("power-reminder", "btn-power");
  }

  showPowerReminder(show) {
    const reminder = document.getElementById("power-reminder");
    if (!reminder) return;

    if (show && localStorage.getItem("a2e-power-reminder-dismissed")) {
      return;
    }

    if (show) {
      this._showReminder(reminder, () => this.repositionPowerReminder(), 'isPowerReminderVisible');
    } else {
      this.isPowerReminderVisible = false;
      reminder.classList.add("hidden");
    }
  }

  dismissPowerReminder() {
    this.showPowerReminder(false);
    localStorage.setItem("a2e-power-reminder-dismissed", "true");
  }

  // BASIC reminder methods (shows when powered on without a disk)

  showBasicReminder(show) {
    const reminder = document.getElementById("basic-reminder");
    if (!reminder) return;

    if (show && localStorage.getItem("a2e-basic-reminder-dismissed")) {
      return;
    }

    if (show) {
      this._showReminder(reminder, () => this.repositionBasicReminder(), 'isBasicReminderVisible');
    } else {
      this.isBasicReminderVisible = false;
      reminder.classList.add("hidden");
    }
  }

  repositionBasicReminder() {
    this.positionReminderBelowElement("basic-reminder", "btn-warm-reset");
  }

  dismissBasicReminder() {
    this.showBasicReminder(false);
    localStorage.setItem("a2e-basic-reminder-dismissed", "true");
  }

  // Mouse capture reminder (shows while a machine with a mouse is running)

  /**
   * Name the modifier the visitor's own keyboard has.
   *
   * Capture is an Alt-click, and Alt is the key marked Option on a Mac and Alt
   * everywhere else — telling a Windows visitor to hold Option names a key
   * their keyboard does not have.
   */
  _nameTheCaptureKey() {
    const el = document.getElementById("mouse-reminder-modifier");
    if (!el) return;
    const platform =
      navigator.userAgentData?.platform || navigator.platform || "";
    const isMac = /mac/i.test(platform);
    el.textContent = isMac ? "\u2325 Option" : "Alt";
  }

  /**
   * Centre the reminder over the bottom of the picture.
   *
   * It is not anchored to a control the way the others are: what it asks for
   * is a click on the screen, so it sits on the screen, near its bottom edge
   * and out of the way of whatever the machine is drawing at the top.
   */
  repositionMouseReminder() {
    const reminder = document.getElementById("mouse-reminder");
    // The canvas, not the monitor frame around it: in the docked layout the
    // screen is moved into a window of its own and the frame is left empty and
    // display:none, so its rectangle is all zeroes and the reminder landed
    // above the top of the page.
    const screen = document.getElementById("screen");
    if (!reminder || !screen) return;

    const rect = screen.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) return;
    const width = reminder.offsetWidth;
    const padding = 16;

    let left = rect.left + rect.width / 2 - width / 2;
    left = Math.max(padding, Math.min(left, window.innerWidth - width - padding));

    reminder.style.left = `${left}px`;
    reminder.style.top = `${rect.bottom - reminder.offsetHeight - MOUSE_REMINDER_INSET_PX}px`;
  }

  /**
   * Show or hide the mouse capture reminder.
   *
   * Unlike the power reminder this one comes back every session — capturing
   * the mouse is a keystroke nobody guesses and few remember — which is why it
   * carries its own "Don't show again" button. That choice is the only thing
   * that stops it.
   */
  showMouseReminder(show) {
    const reminder = document.getElementById("mouse-reminder");
    if (!reminder) return;

    if (show && localStorage.getItem(MOUSE_REMINDER_DISMISSED_KEY)) {
      return;
    }

    if (show) {
      if (this.isMouseReminderVisible) return;
      this._showReminder(
        reminder,
        () => this.repositionMouseReminder(),
        "isMouseReminderVisible",
      );
      this._followScreen();
    } else {
      this.isMouseReminderVisible = false;
      reminder.classList.add("hidden");
    }
  }

  /**
   * Keep the reminder on the picture while it is shown.
   *
   * The screen lives in a window the user can drag as well as resize, and a
   * drag fires no resize event, so the reminder follows the canvas rectangle
   * itself. The loop runs only while the reminder is up and does nothing until
   * the rectangle actually moves.
   */
  _followScreen() {
    if (this._followHandle) return;
    let last = "";
    const tick = () => {
      if (!this.isMouseReminderVisible) {
        this._followHandle = 0;
        return;
      }
      const screen = document.getElementById("screen");
      if (screen) {
        const rect = screen.getBoundingClientRect();
        const key = `${rect.left},${rect.bottom},${rect.width}`;
        if (key !== last) {
          last = key;
          this.repositionMouseReminder();
        }
      }
      this._followHandle = requestAnimationFrame(tick);
    };
    this._followHandle = requestAnimationFrame(tick);
  }

  /** Hide it for good, and remember that across sessions. */
  dismissMouseReminder() {
    this.showMouseReminder(false);
    localStorage.setItem(MOUSE_REMINDER_DISMISSED_KEY, "true");
  }

  /** Has the user asked never to see the mouse reminder again? */
  isMouseReminderDismissed() {
    return !!localStorage.getItem(MOUSE_REMINDER_DISMISSED_KEY);
  }

  /**
   * Reposition all visible reminders (call after resize)
   */
  repositionAll() {
    if (this.isPowerReminderVisible) {
      this.repositionPowerReminder();
    }
    if (this.isBasicReminderVisible) {
      this.repositionBasicReminder();
    }
    if (this.isMouseReminderVisible) {
      this.repositionMouseReminder();
    }
  }
}
