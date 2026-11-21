#include "TDeckContacts.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <stdlib.h>

// One saved contact in /contacts3 (MeshCore's DataStore::saveContacts)
#define REC_SIZE      152
#define REC_FLAGS     65    // bit 0: favourite
#define REC_LASTMOD   140   // when this node last updated it (its own clock)
#define MAX_RECORDS   8192  // more than this: not a contacts file we understand, leave it

struct Rank { uint16_t index; uint8_t fav; uint32_t lastmod; };

static int byKeep(const void* a, const void* b) {  // favourites first, then newest
  const Rank* x = (const Rank*)a;
  const Rank* y = (const Rank*)b;
  if (x->fav != y->fav) return x->fav ? -1 : 1;
  if (x->lastmod != y->lastmod) return x->lastmod > y->lastmod ? -1 : 1;
  return (int)x->index - (int)y->index;
}

static int byIndex(const void* a, const void* b) {
  return (int)((const Rank*)a)->index - (int)((const Rank*)b)->index;
}

// Picks which records of `from` to keep and writes them to /contacts3.
static bool writeKept(const char* from) {
  File in = SPIFFS.open(from, "r");
  if (!in) return false;
  size_t count = in.size() / REC_SIZE;
  Rank* rank = (Rank*)malloc(count * sizeof(Rank));
  if (!rank) { in.close(); return false; }
  uint8_t rec[REC_SIZE];
  for (size_t i = 0; i < count; i++) {
    if (in.read(rec, REC_SIZE) != REC_SIZE) { free(rank); in.close(); return false; }
    rank[i].index = i;
    rank[i].fav = rec[REC_FLAGS] & 1;
    memcpy(&rank[i].lastmod, &rec[REC_LASTMOD], 4);
  }
  size_t keep = count < MAX_CONTACTS ? count : MAX_CONTACTS;
  qsort(rank, count, sizeof(Rank), byKeep);
  qsort(rank, keep, sizeof(Rank), byIndex);  // the kept ones, in their old order

  File out = SPIFFS.open("/contacts3", "w");
  bool ok = (bool)out;
  for (size_t k = 0; ok && k < keep; k++) {
    ok = in.seek((size_t)rank[k].index * REC_SIZE) && in.read(rec, REC_SIZE) == REC_SIZE &&
         out.write(rec, REC_SIZE) == REC_SIZE;
  }
  in.close();
  if (out) out.close();
  free(rank);
  if (ok) Serial.printf("contacts: kept %u of %u (favourites, then newest); all in %s\n",
                        (unsigned)keep, (unsigned)count, from);
  return ok;
}

// /contacts3.tmp marks a trim in progress: if it's there at boot, the last one didn't finish
// (power cut), and it's done again from /contacts3.full. (The first version wrote a temporary
// file and renamed it to /contacts3; on the first T-Deck that rename failed.)
void tdeckTrimContacts() {
  if (!SPIFFS.begin(false)) return;  // (not formatted yet: MeshCore's setup does that)
  if (!SPIFFS.exists("/contacts3.tmp")) {
    File in = SPIFFS.open("/contacts3", "r");
    if (!in) return;
    size_t size = in.size();
    in.close();
    size_t count = size / REC_SIZE;
    if (count <= MAX_CONTACTS || count > MAX_RECORDS || size % REC_SIZE) return;
    File mark = SPIFFS.open("/contacts3.tmp", "w");
    if (!mark) return;
    mark.close();
    SPIFFS.remove("/contacts3.full");  // (an older full list: this one is newer)
    if (!SPIFFS.rename("/contacts3", "/contacts3.full")) { SPIFFS.remove("/contacts3.tmp"); return; }
  } else if (!SPIFFS.exists("/contacts3.full")) {
    SPIFFS.remove("/contacts3.tmp");
    return;
  }
  if (writeKept("/contacts3.full")) SPIFFS.remove("/contacts3.tmp");
  else Serial.println("contacts: couldn't write /contacts3 (tried again next boot)");
}
