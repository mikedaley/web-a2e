/*
 * reminder-controller.js - UI reminder notifications
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * ReminderController - Manages floating reminder tooltips
 * Handles power, resize, and drives toggle reminders with positioning and persistence
 */

export class ReminderController {
  constructor() {
    this.isPowerReminderVisible = false;
    this.isBasicReminderVisible = false;

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
  }
}
