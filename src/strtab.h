// String table: the words the PC game pulls out of castle.exe, loaded from
// plain UTF-8 text files on the memory card instead.
//
// File format (one entry per line):
//     # comment
//     key = value
// Leading/trailing spaces around the key and value are dropped. In values,
// \n is a line break, \t a tab, \\ a backslash, and \s a kept space (for a
// value that must start or end with one). Button prompts are written as
// {tokens} and handled by textlayout (see prompts.h); "{{" is a literal "{".
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

// Same, from a buffer in memory (used by the desktop test).
int strtab_load_mem(const char *text, int len);

// The value for a key, or NULL if there isn't one.
const char *strtab_get(const char *key);

// The game's own text by number: its scripts set TextField.ntext = 742 and
// castle.exe shows string 742. Files made by tools/extract_strings.py use the
// number as the key ("742 = ..."). NULL if missing (logged like a field).
const char *strtab_number(int number);

// Look up the words for one of the game's text fields.
//   swf:         the SWF file name without extension, e.g. "MAIN"
//   path:        the field's target path, e.g. "_root.mainmenu.item2.label"
//   var:         the field's VariableName from DefineEditText (may be NULL/"")
//   placeholder: the field's initial text (the PC game uses "fixme")
// Keys are tried in this order:
//   1. var                      (shared names: the same string everywhere)
//   2. "<swf>:<path>"           (one particular field)
//   3. placeholder, unless it is empty or "fixme"
// When nothing matches, the field is written once to the missing-strings log
// (see strtab_set_missing_log) and NULL is returned, so the caller can keep
// the field blank exactly like now.
const char *strtab_field(const char *swf, const char *path,
                         const char *var, const char *placeholder);

// Where to append fields that have no string yet, e.g.
// "ux0:data/CastleCrashers/strings_missing.txt". NULL turns logging off.
// Each line is written in strtab_load's format with an empty value and a
// comment giving the var and placeholder, so the file can be filled in and
// loaded as-is.
void strtab_set_missing_log(const char *path);

// When on, strtab_field returns the key itself for missing fields, which
// shows on screen where every field is. Off by default.
void strtab_set_show_keys(int on);

// Number of entries currently loaded.
int strtab_count(void);

void strtab_free(void);

#endif
