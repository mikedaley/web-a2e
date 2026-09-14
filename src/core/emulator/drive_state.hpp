/*
 * drive_state.hpp - A pair of 5.25" drives in a save state
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "state_stream.hpp"
#include "../cards/disk_controller.hpp"
#include "../disk-image/disk_image.hpp"

#include <string>

namespace a2e {

/*
 * Every machine here has the same two drives under the same controller, and a
 * save state carries the disks in them — image, filename and where the head
 * is — so that a restored machine finds the disk it was reading. The
 * controller's own registers are the card's business (`serialize()`); this is
 * the media, which the card does not own the format of.
 */
inline void writeDriveState(StateWriter &w, DiskController &disk) {
  for (int drive = 0; drive < 2; drive++) {
    if (!disk.hasDisk(drive)) {
      w.boolean(false);
      continue;
    }
    const DiskImage *image = disk.getDiskImage(drive);
    w.boolean(true);
    w.u16(static_cast<uint16_t>(image->getQuarterTrack()));
    size_t size = 0;
    const uint8_t *data = disk.exportDiskData(drive, &size);
    w.blob(data, data ? size : 0);
    w.string(image->getFilename());
  }
}

inline bool readDriveState(StateReader &r, DiskController &disk) {
  for (int drive = 0; drive < 2; drive++) {
    if (!r.boolean()) {
      if (r.failed()) return false;
      disk.ejectDisk(drive);
      continue;
    }
    const int quarterTrack = r.u16();
    size_t size = 0;
    const uint8_t *data = r.blob(size);
    const std::string filename = r.string();
    if (r.failed()) return false;
    if (data && size > 0) {
      disk.insertDisk(drive, data, size, filename.empty() ? "state.dsk" : filename);
      if (DiskImage *image = disk.getMutableDiskImage(drive)) {
        image->setQuarterTrack(quarterTrack);
      }
    } else {
      disk.ejectDisk(drive);
    }
  }
  return true;
}

} // namespace a2e
