// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Adverts: send one now (nearby = zero hop, or to the whole mesh = flood), and on a
// schedule (Settings > Auto advert), like repeaters do, so other nodes keep a fresh path to
// this one. Off by default; the intervals are long so the mesh isn't flooded: nearby every
// 1-12 h, mesh every 12-48 h. The first one is a full interval after boot.

static const uint8_t ADV_NEARBY_HOURS[] = {0, 1, 3, 6, 12};  // 0 = off
static const uint8_t ADV_MESH_HOURS[] = {0, 12, 24, 48};
#define NUM_ADV_NEARBY (sizeof(ADV_NEARBY_HOURS) / sizeof(ADV_NEARBY_HOURS[0]))
#define NUM_ADV_MESH   (sizeof(ADV_MESH_HOURS) / sizeof(ADV_MESH_HOURS[0]))

static void advHoursLabel(char* buf, uint8_t hours) {
  if (hours == 0) strcpy(buf, "Off");
  else sprintf(buf, "%d h", hours);
}

// as the app's "send advert" does (MyMesh CMD_SEND_SELF_ADVERT): location per the node's
// advert location setting, a mesh advert in the default scope
bool UITask::sendAdvert(bool mesh_wide) {
  NodePrefs* np = the_mesh.getNodePrefs();
  mesh::Packet* pkt = np->advert_loc_policy == ADVERT_LOC_NONE
                          ? the_mesh.createSelfAdvert(np->node_name)
                          : the_mesh.createSelfAdvert(np->node_name, sensors.node_lat, sensors.node_lon);
  if (!pkt) return false;
  if (!mesh_wide) {
    the_mesh.sendZeroHop(pkt);
  } else {
    TransportKey scope;
    memcpy(&scope.key, np->default_scope_key, sizeof(scope.key));
    if (scope.isNull()) {
      the_mesh.sendFlood(pkt, (uint32_t)0, (uint8_t)(np->path_hash_mode + 1));
    } else {
      uint16_t codes[2] = {scope.calcTransportCode(pkt), 0};
      the_mesh.sendFlood(pkt, codes, (uint32_t)0, (uint8_t)(np->path_hash_mode + 1));
    }
  }
  return true;
}

void UITask::advertLoop() {
  unsigned long now = millis();
  uint8_t nh = ADV_NEARBY_HOURS[_ui_prefs.adv_nearby % NUM_ADV_NEARBY];
  uint8_t mh = ADV_MESH_HOURS[_ui_prefs.adv_mesh % NUM_ADV_MESH];
  // (re)armed when the setting changes: a full interval from now
  if (nh != _adv_nearby_h) { _adv_nearby_h = nh; _adv_nearby_at = now + nh * 3600000UL; }
  if (mh != _adv_mesh_h) { _adv_mesh_h = mh; _adv_mesh_at = now + mh * 3600000UL; }
  if (mh && (long)(now - _adv_mesh_at) >= 0) {
    _adv_mesh_at = now + mh * 3600000UL;
    sendAdvert(true);
    // a mesh advert reaches the nearby nodes too: the nearby one can wait a full interval
    if (nh) _adv_nearby_at = now + nh * 3600000UL;
  } else if (nh && (long)(now - _adv_nearby_at) >= 0) {
    _adv_nearby_at = now + nh * 3600000UL;
    sendAdvert(false);
  }
}
