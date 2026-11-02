// mesh::RTCClock on the SF32's on-chip RTC (32.768 kHz crystal). The time survives a
// reboot and the menu's power off (PMU shutdown keeps the RTC running); it's lost when
// the chip loses power entirely (battery empty / shipping mode, battery switch) and on
// flashing. Once synced (2025+), it never moves backwards (see setCurrentTime).
//
// timeKnown(): whether the time is real (set by the phone / CLI since it was last lost,
// or agreed by several nodes on the mesh: setFromMesh), as opposed to MeshCore's guess
// from contact timestamps after a loss. Kept across reboots in /sf32_timeok ("m" inside:
// from the mesh).
#pragma once

#include <Mesh.h>

class SF32RTCClock : public mesh::RTCClock {
public:
  void begin();
  void reset();  // back to the default start time (and "not known")
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;

  void afterSetup();  // once MeshCore's setup() is done (filesystem mounted)
  bool timeKnown() const { return _source != SRC_NONE; }
  bool timeFromMesh() const { return _source == SRC_MESH; }
  // Only while the time isn't known: set it from the mesh (may go backwards). The phone
  // or CLI still replaces it (MeshCore only accepts later times from them).
  void setFromMesh(uint32_t time);

private:
  enum Source : uint8_t { SRC_NONE, SRC_MESH, SRC_PHONE };
  void setSource(Source s);
  bool _lost_at_boot = false, _setup_done = false;
  Source _source = SRC_NONE;
};
