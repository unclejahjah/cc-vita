// String table: the words the PC game pulls out of castle.exe, loaded from
// plain UTF-8 text files on the memory card instead.
//
// File format (one entry per line):
//     # comment
//     key = value
// Leading/trailing spaces around the key and value are dropped. In values,
// \n is a line break, \t a tab, \\ a backslash, and \s a kept space (for a
// value that must start or end with one).
// A UTF-8 byte-order mark at the start of the file is skipped.
//
// Files loaded later override earlier ones key by key, so load the game's
// strings first and the Vita overrides after them.
#ifndef STRTAB_H
#define STRTAB_H

// Load one file. Returns the number of entries read, or -1 if the file
// can't be opened (a missing file is not an error for the caller to show;
// it just means fewer strings).
int strtab_load(const char *path);

// The value for a key, or NULL if there isn't one.
const char *strtab_get(const char *key);

// The game's own text by number: its scripts set TextField.ntext = 742 and
// castle.exe shows string 742. Files made by tools/extract_strings.py use the
// number as the key ("742 = ..."). NULL if missing.
const char *strtab_number(int number);

#endif
