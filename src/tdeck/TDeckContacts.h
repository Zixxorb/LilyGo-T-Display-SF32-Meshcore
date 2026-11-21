// Contacts saved by a firmware that keeps more than MAX_CONTACTS (meshcomod kept 1,076):
// MeshCore loads the first MAX_CONTACTS in file order and drops the rest at its next save.
// Before that happens, once, this keeps the favourites and then the most recently updated
// contacts, and moves the full file to /contacts3.full (nothing is deleted).
#pragma once

void tdeckTrimContacts();
