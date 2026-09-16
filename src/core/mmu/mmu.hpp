/*
 * mmu.hpp - Memory management unit with soft switches
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../machine/machine_profile.hpp"
#include "../types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace a2e {

// Forward declarations
class ExpansionCard;
class MouseIOU;
class NoSlotClock;

/**
 * The soft switches as one word, for a debugger to look at.
 *
 * Both machines' //e switches are the same switches — a IIgs's are its Mega
 * II's — so the packing is here rather than on either machine. The three
 * pushbuttons and "a key is waiting" are passed in because they do not come
 * from the switches: on a //e they are the game connector and the keyboard
 * latch, and on a IIgs they come from the ADB.
 */
uint64_t packSoftSwitchState(const SoftSwitches &sw, bool button0, bool button1,
                             bool button2, bool keyAvailable);

class MMU {
public:
  using KeyboardCallback = std::function<uint8_t()>;
  using KeyStrobeCallback = std::function<void()>;
  using AnyKeyDownCallback = std::function<bool()>;   // Returns true if any key is currently held
  using SpeakerCallback = std::function<void()>;
  using ButtonCallback = std::function<uint8_t(int)>; // Returns button state for button 0-2
  using CycleCallback = std::function<uint64_t()>;    // Returns current CPU cycle count
  using VideoSwitchCallback = std::function<void()>;  // Called when video-relevant switches change
  using WatchpointReadCallback = std::function<void(uint16_t, uint8_t)>;
  using WatchpointWriteCallback = std::function<void(uint16_t, uint8_t)>;

  // The profile supplies video timing: the floating-bus scanner and the VBL
  // status bit are both derived from where in the frame the machine is, and a
  // frame is a count of cycles that differs between machines.
  explicit MMU(const MachineProfile &machine = defaultMachineProfile());
  ~MMU();  // Defined in mmu.cpp (needed for unique_ptr<ExpansionCard>)

  // The machine this MMU is modelling.
  const MachineProfile &getMachine() const { return *machine_; }

  // Base of the address window systemROM_ covers. Every ROM read indexes the
  // array as `address - ROM_WINDOW_BASE`, and loadROM() places a machine's
  // image at the offset its own ROM base implies.
  static constexpr uint16_t ROM_WINDOW_BASE = 0xC000;

  // Memory access
  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t value);

  // Non-side-effecting read for debugger/memory viewer
  uint8_t peek(uint16_t address) const;

  // Non-side-effecting read of auxiliary memory (for text selection in 80-col mode)
  uint8_t peekAux(uint16_t address) const;

  // Direct memory access (bypasses soft switches)
  uint8_t readRAM(uint16_t address, bool aux = false) const;
  void writeRAM(uint16_t address, uint8_t value, bool aux = false);

  /**
   * The language card's RAM at $D000-$FFFF, with main or auxiliary named by
   * the caller instead of taken from ALTZP.
   *
   * A //e has one language card and ALTZP decides which half of it is in the
   * map — there is no other way to reach it, so `readLanguageCard` asks the
   * switch and that is the whole story. A IIgs reaches the same hardware two
   * ways: through the map, as a //e does, and by addressing bank $E0 or $E1
   * directly, where the *bank number* names the half. Those are two different
   * 48K, and the toolbox and GS/OS live in $E1's.
   *
   * The read is RAM, not ROM: whether the space reads the card or the machine's
   * ROM is the caller's to decide, because a IIgs asks a different register
   * about it. The write still honours the card's write enable, which is the
   * same switch either way in.
   */
  uint8_t readLanguageCardRAM(uint16_t address, bool aux) const;
  void writeLanguageCardRAM(uint16_t address, uint8_t value, bool aux);

  // Language card RAM access (for state serialization)
  const uint8_t *getLCBank1(bool aux = false) const {
    return aux ? auxLcBank1_.data() : lcBank1_.data();
  }
  const uint8_t *getLCBank2(bool aux = false) const {
    return aux ? auxLcBank2_.data() : lcBank2_.data();
  }
  const uint8_t *getLCHighRAM(bool aux = false) const {
    return aux ? auxLcHighRAM_.data() : lcHighRAM_.data();
  }
  void setLCBank1(const uint8_t *data, bool aux = false) {
    auto &bank = aux ? auxLcBank1_ : lcBank1_;
    std::copy(data, data + bank.size(), bank.begin());
  }
  void setLCBank2(const uint8_t *data, bool aux = false) {
    auto &bank = aux ? auxLcBank2_ : lcBank2_;
    std::copy(data, data + bank.size(), bank.begin());
  }
  void setLCHighRAM(const uint8_t *data, bool aux = false) {
    auto &bank = aux ? auxLcHighRAM_ : lcHighRAM_;
    std::copy(data, data + bank.size(), bank.begin());
  }

  // ROM loading - combined 16KB system ROM ($C000-$FFFF)
  void loadROM(const uint8_t *systemRom, size_t systemSize,
               const uint8_t *charRom, size_t charSize);

  // Character ROM access (for video)
  uint8_t readCharROM(uint16_t address) const;

  // Soft switch state
  const SoftSwitches &getSoftSwitches() const { return switches_; }

  /**
   * What an unread address reads: whatever the video scanner is fetching.
   *
   * Public because a IIgs's memory controller sits in front of this one and
   * has to answer for an empty socket itself — a slot the Slot register has
   * switched to a card that is not there drives nothing, and the machine's own
   * device for that slot must not answer in its place.
   */
  uint8_t floatingBus() { return getFloatingBusValue(); }

  /**
   * The switches a save state carries, as one word, and how they come back.
   *
   * Restoring is done by writing the switches' own addresses rather than
   * poking the struct, so that everything that watches a switch — the video,
   * the language card's bank pointers, a machine that has no auxiliary bank
   * and ignores the whole group — sees the change the way it always does. The
   * language card takes the double read its write latch needs. Both machines
   * use this: a IIgs's Mega II is this class, restored the same way.
   */
  uint32_t packSwitchesForState() const;
  void restoreSwitchesFromState(uint32_t packed);

  // Callbacks
  void setKeyboardCallback(KeyboardCallback cb) {
    keyboardCallback_ = std::move(cb);
  }
  void setKeyStrobeCallback(KeyStrobeCallback cb) {
    keyStrobeCallback_ = std::move(cb);
  }
  void setAnyKeyDownCallback(AnyKeyDownCallback cb) {
    anyKeyDownCallback_ = std::move(cb);
  }
  void setSpeakerCallback(SpeakerCallback cb) {
    speakerCallback_ = std::move(cb);
  }
  void setButtonCallback(ButtonCallback cb) { buttonCallback_ = std::move(cb); }
  void setCycleCallback(CycleCallback cb) { cycleCallback_ = std::move(cb); }
  void setVideoSwitchCallback(VideoSwitchCallback cb) { videoSwitchCallback_ = std::move(cb); }
  void setWatchpointCallbacks(WatchpointReadCallback readCb, WatchpointWriteCallback writeCb) {
    watchpointReadCallback_ = std::move(readCb);
    watchpointWriteCallback_ = std::move(writeCb);
  }
  void setWatchpointsActive(bool active) { watchpointsActive_ = active; }

  // Paddle/joystick input
  void setPaddleValue(int paddle, uint8_t value) {
    if (paddle >= 0 && paddle < 4) {
      paddleValues_[paddle] = value;
    }
  }
  uint8_t getPaddleValue(int paddle) const {
    return (paddle >= 0 && paddle < 4) ? paddleValues_[paddle] : 128;
  }


  // ===== Expansion Slot Management =====

  /**
   * Insert a card into an expansion slot
   * @param slot Slot number (1-7)
   * @param card Card to insert (ownership transferred)
   * @return Previously installed card, or nullptr if slot was empty
   */
  std::unique_ptr<ExpansionCard> insertCard(uint8_t slot, std::unique_ptr<ExpansionCard> card);

  /**
   * Remove a card from an expansion slot
   * @param slot Slot number (1-7)
   * @return The removed card, or nullptr if slot was empty
   */
  std::unique_ptr<ExpansionCard> removeCard(uint8_t slot);

  /**
   * Get a card from a slot (non-owning)
   * @param slot Slot number (1-7)
   * @return Pointer to card, or nullptr if slot is empty
   */
  ExpansionCard* getCard(uint8_t slot) const;

  /**
   * Check if a slot is empty
   * @param slot Slot number (1-7)
   * @return true if no card is installed
   */
  bool isSlotEmpty(uint8_t slot) const;

  /**
   * Get which slot's expansion ROM is active
   * @return Active slot (1-7), or 0 if none
   */
  uint8_t getActiveExpansionSlot() const { return activeExpansionSlot_; }

  // Whether the video is in vertical blanking, which $C019 reports and a //c's
  // mouse interrupts on.
  bool isInVerticalBlank() const;

  // A //c's mouse, which is IOU soft switches rather than a card in a slot.
  // Null on every other machine, and the null is what keeps those machines'
  // $C015, $C063 and $C066 exactly as they were.
  void setMouseIOU(MouseIOU *mouse) { mouseIOU_ = mouse; }
  MouseIOU *getMouseIOU() const { return mouseIOU_; }

  // No-Slot Clock (DS1215)
  void enableNoSlotClock(bool enable);
  bool isNoSlotClockEnabled() const;
  NoSlotClock* getNoSlotClock() { return noSlotClock_.get(); }

  // Reset
  void reset();
  void warmReset();  // Reset soft switches and cards, preserve RAM

  // Memory access tracking for debugger heat map
  void enableTracking(bool enable) { trackingEnabled_ = enable; }
  bool isTrackingEnabled() const { return trackingEnabled_; }
  void clearTracking();
  void decayTracking(uint8_t amount = 1); // Reduce all counts for real-time visualization
  const uint8_t* getReadCounts() const { return readCounts_.data(); }
  const uint8_t* getWriteCounts() const { return writeCounts_.data(); }

  // Direct memory array access for heat map visualization
  const uint8_t* getMainRAM() const { return mainRAM_.data(); }
  const uint8_t* getAuxRAM() const { return auxRAM_.data(); }
  const uint8_t* getSystemROM() const { return systemROM_.data(); }

  /**
   * The address the video scanner is fetching at a given cycle.
   *
   * The scanner runs continuously and addresses memory during horizontal and
   * vertical blanking as well as over the visible picture, which is what makes
   * the floating bus a usable clock: a program can read an undriven soft
   * switch and learn where the beam is.
   *
   * Implements the counter equations from Sather, Understanding the Apple IIe
   * (5-8 T5.1, 5-9), so the addresses agree with hardware across the whole
   * line rather than only over the 40 visible cycles.
   *
   * @param cycles Absolute CPU cycle count
   * @return Address in main/aux RAM the scanner reads at that cycle
   */
  uint16_t getVideoScannerAddress(uint64_t cycles) const;

private:
  // Soft switch handling
  uint8_t readSoftSwitch(uint16_t address);
  uint8_t peekSoftSwitch(uint16_t address) const;
  void writeSoftSwitch(uint16_t address, uint8_t value);

  // Floating bus - returns value video hardware is currently reading
  uint8_t getFloatingBusValue();

  // Language card logic
  uint8_t readLanguageCard(uint16_t address);
  void writeLanguageCard(uint16_t address, uint8_t value);
  uint8_t handleLanguageCardSwitch(uint8_t reg);
  void handleLanguageCardSwitchWrite(uint8_t reg);

  // Memory banks
  // Rewrites a freshly loaded character ROM into the single layout the video
  // renderer reads, per the machine's MachineCharRom.
  void normaliseCharROM(size_t length);

  // Not owned: profiles are static constexpr objects with program lifetime.
  const MachineProfile *machine_ = &defaultMachineProfile();

  std::array<uint8_t, MAIN_RAM_SIZE> mainRAM_{};
  std::array<uint8_t, AUX_RAM_SIZE> auxRAM_{};

  // Language card RAM banks
  std::array<uint8_t, 0x1000> lcBank1_{};   // $D000-$DFFF bank 1
  std::array<uint8_t, 0x1000> lcBank2_{};   // $D000-$DFFF bank 2
  std::array<uint8_t, 0x2000> lcHighRAM_{}; // $E000-$FFFF

  // Auxiliary language card banks
  std::array<uint8_t, 0x1000> auxLcBank1_{};
  std::array<uint8_t, 0x1000> auxLcBank2_{};
  std::array<uint8_t, 0x2000> auxLcHighRAM_{};

  // ROM - combined 16KB system ROM ($C000-$FFFF)
  std::array<uint8_t, 0x4000> systemROM_{}; // $C000-$FFFF (16KB)
  std::array<uint8_t, CHAR_ROM_SIZE> charROM_{};

  // Soft switches
  SoftSwitches switches_;

  // Keyboard state
  uint8_t keyboardLatch_ = 0;

  // Paddle/joystick state
  std::array<uint8_t, 4> paddleValues_ = {128, 128, 128, 128}; // Centered by default
  uint64_t paddleTriggerCycle_ = 0;
  static constexpr int PADDLE_CYCLES_PER_UNIT = 11; // ~11 cycles per paddle unit

  // Callbacks
  KeyboardCallback keyboardCallback_;
  KeyStrobeCallback keyStrobeCallback_;
  AnyKeyDownCallback anyKeyDownCallback_;
  SpeakerCallback speakerCallback_;
  ButtonCallback buttonCallback_;
  CycleCallback cycleCallback_;
  VideoSwitchCallback videoSwitchCallback_;
  WatchpointReadCallback watchpointReadCallback_;
  WatchpointWriteCallback watchpointWriteCallback_;
  bool watchpointsActive_ = false;

  // No-Slot Clock (DS1215)
  std::unique_ptr<NoSlotClock> noSlotClock_;
  MouseIOU *mouseIOU_ = nullptr; // Not owned: the Emulator holds it

  // Expansion slots (1-7, index 0-6)
  // Indexed by slot number, so slots_[6] is slot 6. There are eight entries
  // rather than seven because slot 0 is a real slot on a II+ — it is where the
  // 16K language card goes — even though a //e has nothing there. Which of
  // them exist on a given machine is the profile's answer, not this array's.
  std::array<std::unique_ptr<ExpansionCard>, MACHINE_SLOT_COUNT> slots_;
  uint8_t activeExpansionSlot_ = 0;  // Which card owns $C800-$CFFF (0 = none)

  // Memory access tracking for debugger heat map
  bool trackingEnabled_ = false;
  std::array<uint8_t, 65536> readCounts_{};
  std::array<uint8_t, 65536> writeCounts_{};
};

} // namespace a2e
