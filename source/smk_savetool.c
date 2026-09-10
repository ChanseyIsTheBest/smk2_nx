/* smk_savetool.c -- edit the game's own save files at boot, from saves.txt.
 *
 * MIT licensed. See LICENSE.
 *
 * FORMAT
 * ------
 * Bloons Supermonkey saves are JSON behind two layers, both worked out by
 * diffing two saves 500 currency apart and then confirming each piece against
 * the engine's own code in libnative.so:
 *
 *     offset 0    "DGDATA"                  6 bytes
 *     offset 6    %08x of a CRC-32          8 ASCII hex chars
 *     offset 14   obfuscated JSON           to end of file
 *
 * The engine builds that header with the format string "DGDATA%08x", which is
 * sitting in the binary at 0xb27eb2.
 *
 * Obfuscation is a positional additive shift on a 6-byte cycle:
 *
 *     cipher[i] = (plain[i] + 21 + (i % 6)) % 256
 *
 * There is no key; 21 and the 6-cycle are constants. Found by autocorrelation
 * (every strong shift was a multiple of 6), then the "printable ASCII only"
 * constraint pinned each of the six columns to one offset, stepping 21..26.
 *
 * The checksum is a reflected CRC-32 (poly 0xEDB88320) over the PLAINTEXT,
 * with two deviations from every stock implementation:
 *
 *   - init is 0 and there is no final XOR (zlib uses 0xFFFFFFFF for both);
 *   - of the eight shift rounds per byte the first is LOGICAL and the other
 *     seven are ARITHMETIC. Once the polynomial XOR sets bit 31 that
 *     sign-extends, so the result diverges from a normal CRC-32.
 *
 * That second one is almost certainly a signed/unsigned slip in Ninja Kiwi's
 * original C, but it is baked into every save the game has ever written, so it
 * is the spec. It is why no off-the-shelf CRC matches and why crc_nk() below
 * spells the loop out by hand.
 *
 * WHY EDIT IN PLACE RATHER THAN REGENERATE
 * ----------------------------------------
 * Values are substituted textually in the decoded JSON, leaving every other
 * byte exactly as the engine wrote it. Reserialising would mean shipping a
 * JSON writer here and guaranteeing it round-trips the engine's key order,
 * number spelling and float formatting -- and any difference is a save the
 * engine may reject, on data the player cannot easily get back. A targeted
 * substitution cannot introduce a field the engine did not write.
 *
 * WHICH FILES
 * -----------
 * Every *.save in the game folder that actually starts with DGDATA. The engine
 * only spells one save name literally in the binary (PublicProfile.save) and
 * writes others at runtime (TempProfile.save turned up in the boot log), so
 * scanning beats a hardcoded list: it picks up profile slots whatever they are
 * called, and skips anything that is not one of these files.
 *
 * A .bak of each original is written once, before the first edit, and never
 * overwritten.
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#include "config.h"
#include "smk_savetool.h"
#include "nx_data_root.h"
#include "util.h"

#define MAX_SAVE   262144           /* real saves are ~14 KB; leave headroom */
#define SHIFT_BASE 21
#define SHIFT_CYC  6
#define CRC_POLY   0xEDB88320u

/* Research nodes, in the order researchLevelsObject lists them. Names are the
 * JSON keys, so what you type in saves.txt is what is in the file. */
static const char *RESEARCH[] = {
  "sharpDarts", "bloontoniumDarts", "megaRangs", "hotRangs",
  "heavyOrdinance", "phosphorusBombs", "technologicalTerror", "focusedBeams",
  "wizardLord", "earthMagic", "bigBloonSabotage", "forceFieldPhasing",
  "frostbite", "deepFreeze", "severeWeather", "eyeOfTheStorm",
};
#define N_RESEARCH ((int)(sizeof RESEARCH / sizeof *RESEARCH))

/* Weapon categories, as they appear inside each loadout slot:
 *     {"name":"dart","level":1}
 * The same category appears once per slot, so setting one sets every slot. */
static const char *WEAPONS[] = {
  "dart", "boomerang", "bomb", "magic", "energy", "ice", "storm",
};
#define N_WEAPONS ((int)(sizeof WEAPONS / sizeof *WEAPONS))

/* -1 / -2 mean "not set": leave whatever the game already has. */
static long g_money_weapons  = -1;
static long g_money_research = -1;
static long g_research[N_RESEARCH];
static long g_weapon[N_WEAPONS];
static int  g_detected_hacks = -1;
static int  g_music = -1, g_sfx = -1, g_premium = -1;
static int  g_any;

/* ------------------------------------------------------------------ */
/* checksum                                                            */
/* ------------------------------------------------------------------ */

static unsigned crc_nk(const unsigned char *d, size_t n) {
  unsigned crc = 0;
  size_t i;
  int r;
  for (i = 0; i < n; i++) {
    unsigned x = (d[i] ^ (crc & 0xFF)) & 0xFF;
    /* round 1: logical shift */
    x = (x & 1) ? ((x >> 1) ^ CRC_POLY) : (x >> 1);
    /* rounds 2-8: arithmetic shift -- see the note at the top of this file */
    for (r = 0; r < 7; r++) {
      unsigned s = (x >> 1) | ((x & 0x80000000u) ? 0x80000000u : 0u);
      x = (x & 1) ? (s ^ CRC_POLY) : s;
    }
    crc = x ^ (crc >> 8);
  }
  return crc;
}

static void deobfuscate(unsigned char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) b[i] = (unsigned char)(b[i] - SHIFT_BASE - (i % SHIFT_CYC));
}

static void obfuscate(unsigned char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) b[i] = (unsigned char)(b[i] + SHIFT_BASE + (i % SHIFT_CYC));
}

/* ------------------------------------------------------------------ */
/* saves.txt                                                           */
/* ------------------------------------------------------------------ */

static void write_template(const char *path) {
  FILE *f = fopen(path, "w");
  int i;
  if (!f) { debugPrintf("savetool: cannot create %s\n", path); return; }

  fputs(
    "# smk_nx -- save editor\n"
    "#\n"
    "# Everything here is commented out, so by default this file does nothing.\n"
    "# Uncomment a line and set a value to have it written into your save the\n"
    "# next time the game starts.\n"
    "#\n"
    "# The edit happens ONCE PER BOOT, before the game loads anything. After\n"
    "# that the game owns the value again -- spend the money and it goes down,\n"
    "# and it will not come back until you restart. Leave a line uncommented\n"
    "# and it is re-applied on every launch.\n"
    "#\n"
    "# Every *.save in this folder is patched, so it does not matter which\n"
    "# profile slot you are on. The original of each file is copied to\n"
    "# <name>.save.bak the first time, and never overwritten after that.\n"
    "#\n"
    "# Format: 'name = value'. '#' starts a comment anywhere on a line.\n"
    "\n"
    "# --- currency ------------------------------------------------------\n"
    "# money_weapons is the cash you spend on weapons and upgrades.\n"
    "# money_research is the separate research currency.\n"
    "#money_weapons = 100000\n"
    "#money_research = 10000\n"
    "\n"
    "# --- research ------------------------------------------------------\n"
    "# Levels for each research node. The game's own maximum is small; very\n"
    "# large numbers are written faithfully but the UI is not designed to\n"
    "# draw them.\n"
    "\n", f);

  for (i = 0; i < N_RESEARCH; i++)
    fprintf(f, "#research.%s = 0\n", RESEARCH[i]);

  fputs(
    "\n"
    "# --- weapons -------------------------------------------------------\n"
    "# Level of each weapon category in your loadout. A category appears once\n"
    "# per slot (right / core / ...), and setting it here sets every slot.\n"
    "\n", f);

  for (i = 0; i < N_WEAPONS; i++)
    fprintf(f, "#weapon.%s = 0\n", WEAPONS[i]);

  fputs(
    "\n"
    "# --- misc ----------------------------------------------------------\n"
    "# The game keeps its own tamper counter. If it ever becomes non-zero and\n"
    "# you want it cleared, uncomment this. What sets it, and what the game\n"
    "# does about it, is not known -- keep your .bak either way.\n"
    "#detected_hacks = 0\n"
    "\n"
    "# Audio toggles, the same ones the in-game options menu writes.\n"
    "#music = on\n"
    "#sfx = on\n"
    "\n"
    "# The premium flag. This is a local boolean, not a purchase -- it is not\n"
    "# known what the game gates behind it, and nothing here contacts a store.\n"
    "#premium = off\n"
    "\n", f);

  fclose(f);
  debugPrintf("savetool: wrote template %s\n", path);
}

static int truthy(const char *v) {
  return !strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "yes") || !strcmp(v, "true");
}

static void trim(char *s) {
  char *p = s;
  size_t n;
  while (*p && isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int read_settings(void) {
  char path[600], line[256];
  FILE *f;
  int i;

  for (i = 0; i < N_RESEARCH; i++) g_research[i] = -1;
  for (i = 0; i < N_WEAPONS;  i++) g_weapon[i]   = -1;

  snprintf(path, sizeof path, "%s/saves.txt", g_data_root);
  f = fopen(path, "r");
  if (!f) { write_template(path); return 0; }

  while (fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#');
    char *eq, *key, *val;
    if (hash) *hash = 0;
    eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    key = line; val = eq + 1;
    trim(key); trim(val);
    if (!*key || !*val) continue;

    if (!strcmp(key, "money_weapons"))       { g_money_weapons  = strtol(val, NULL, 10); g_any = 1; }
    else if (!strcmp(key, "money_research")) { g_money_research = strtol(val, NULL, 10); g_any = 1; }
    else if (!strcmp(key, "detected_hacks")) { g_detected_hacks = (int)strtol(val, NULL, 10); g_any = 1; }
    else if (!strcmp(key, "music"))          { g_music   = truthy(val); g_any = 1; }
    else if (!strcmp(key, "sfx"))            { g_sfx     = truthy(val); g_any = 1; }
    else if (!strcmp(key, "premium"))        { g_premium = truthy(val); g_any = 1; }
    else if (!strncmp(key, "research.", 9)) {
      for (i = 0; i < N_RESEARCH; i++)
        if (!strcmp(key + 9, RESEARCH[i])) { g_research[i] = strtol(val, NULL, 10); g_any = 1; break; }
      if (i == N_RESEARCH) debugPrintf("savetool: unknown research \"%s\"\n", key + 9);
    }
    else if (!strncmp(key, "weapon.", 7)) {
      for (i = 0; i < N_WEAPONS; i++)
        if (!strcmp(key + 7, WEAPONS[i])) { g_weapon[i] = strtol(val, NULL, 10); g_any = 1; break; }
      if (i == N_WEAPONS) debugPrintf("savetool: unknown weapon \"%s\"\n", key + 7);
    }
    else debugPrintf("savetool: unknown setting \"%s\"\n", key);
  }
  fclose(f);
  return g_any;
}

/* ------------------------------------------------------------------ */
/* JSON value substitution                                             */
/* ------------------------------------------------------------------ */

/* Replace the number after the first `pat` at or after `from`. The buffer is
 * NUL-terminated and grows or shrinks in place; `cap` bounds it. Returns a
 * pointer just past the replacement, or NULL if nothing matched -- so callers
 * can walk every occurrence. */
static char *set_num_at(char *buf, size_t cap, char *from, const char *pat, long value) {
  char num[24];
  char *at, *p, *end;
  size_t tail, numlen;

  at = strstr(from ? from : buf, pat);
  if (!at) return NULL;

  p = at + strlen(pat);
  while (*p == ' ') p++;
  end = p;
  if (*end == '-') end++;
  while (isdigit((unsigned char)*end)) end++;
  if (end == p) return NULL;                    /* not a number: leave it */

  numlen = (size_t)snprintf(num, sizeof num, "%ld", value);
  tail = strlen(end) + 1;
  if ((size_t)(p - buf) + numlen + tail > cap) return NULL;

  memmove(p + numlen, end, tail);
  memcpy(p, num, numlen);
  return p + numlen;
}

static int set_key(char *buf, size_t cap, const char *jkey, long value) {
  char pat[96];
  snprintf(pat, sizeof pat, "\"%s\":", jkey);
  return set_num_at(buf, cap, buf, pat, value) != NULL;
}

/* Set every occurrence -- weapon categories repeat once per loadout slot. */
static int set_key_all(char *buf, size_t cap, const char *pat, long value) {
  char *p = buf;
  int n = 0;
  while ((p = set_num_at(buf, cap, p, pat, value)) != NULL) n++;
  return n;
}

static int set_bool(char *buf, size_t cap, const char *jkey, int value) {
  char pat[96];
  char *at, *p, *end;
  const char *rep = value ? "true" : "false";
  size_t tail, rlen;

  snprintf(pat, sizeof pat, "\"%s\":", jkey);
  at = strstr(buf, pat);
  if (!at) return 0;
  p = at + strlen(pat);
  while (*p == ' ') p++;
  if (!strncmp(p, "true", 4))       end = p + 4;
  else if (!strncmp(p, "false", 5)) end = p + 5;
  else return 0;

  rlen = strlen(rep);
  tail = strlen(end) + 1;
  if ((size_t)(p - buf) + rlen + tail > cap) return 0;
  memmove(p + rlen, end, tail);
  memcpy(p, rep, rlen);
  return 1;
}

static int patch_json(char *j, size_t cap) {
  char pat[96];
  int n = 0, i;

  if (g_money_weapons  >= 0) n += set_key(j, cap, "money_weapons",  g_money_weapons);
  if (g_money_research >= 0) n += set_key(j, cap, "money_research", g_money_research);
  if (g_detected_hacks >= 0) n += set_key(j, cap, "DetectedHacks",  g_detected_hacks);

  for (i = 0; i < N_RESEARCH; i++)
    if (g_research[i] >= 0) n += set_key(j, cap, RESEARCH[i], g_research[i]);

  for (i = 0; i < N_WEAPONS; i++)
    if (g_weapon[i] >= 0) {
      /* Match the pair, not just "level": every slot has one of each category
       * and the levels sit next to their own names. */
      snprintf(pat, sizeof pat, "\"name\":\"%s\",\"level\":", WEAPONS[i]);
      n += set_key_all(j, cap, pat, g_weapon[i]);
    }

  if (g_music   >= 0) n += set_bool(j, cap, "music_on",  g_music);
  if (g_sfx     >= 0) n += set_bool(j, cap, "sfx_on",    g_sfx);
  if (g_premium >= 0) n += set_bool(j, cap, "IsPremium", g_premium);

  return n;
}

/* ------------------------------------------------------------------ */
/* patching one file                                                   */
/* ------------------------------------------------------------------ */

static void backup_once(const char *path, const unsigned char *raw, size_t n) {
  char bak[700];
  struct stat st;
  FILE *out;

  snprintf(bak, sizeof bak, "%s.bak", path);
  if (stat(bak, &st) == 0) return;             /* already have the original */
  out = fopen(bak, "wb");
  if (!out) return;
  fwrite(raw, 1, n, out);
  fclose(out);
  debugPrintf("savetool: backed up %s\n", bak);
}

static void patch_file(const char *path, const char *name) {
  static unsigned char buf[MAX_SAVE + 1];
  FILE *f;
  size_t n, blen;
  unsigned stored, computed;
  char *json;
  int edits;

  f = fopen(path, "rb");
  if (!f) return;
  n = fread(buf, 1, MAX_SAVE, f);
  fclose(f);

  if (n <= 14 || memcmp(buf, "DGDATA", 6) != 0) return;   /* not one of ours */

  {
    char hex[9];
    memcpy(hex, buf + 6, 8); hex[8] = 0;
    stored = (unsigned)strtoul(hex, NULL, 16);
  }

  blen = n - 14;
  json = (char *)buf + 14;
  deobfuscate((unsigned char *)json, blen);
  json[blen] = 0;

  if (json[0] != '{') {
    debugPrintf("savetool: %s did not decode to JSON -- skipped\n", name);
    return;
  }

  computed = crc_nk((const unsigned char *)json, blen);
  if (computed != stored)
    debugPrintf("savetool: %s checksum was already wrong (%08x != %08x)\n",
                name, stored, computed);

  edits = patch_json(json, MAX_SAVE - 14);
  if (edits == 0) return;                       /* nothing this file has */

  /* Re-encode. Back up FIRST, from a freshly read copy of the original -- the
   * buffer in hand has already been decoded and edited. */
  {
    static unsigned char orig[MAX_SAVE];
    FILE *in = fopen(path, "rb");
    if (in) {
      size_t on = fread(orig, 1, MAX_SAVE, in);
      fclose(in);
      backup_once(path, orig, on);
    }
  }

  blen = strlen(json);
  computed = crc_nk((const unsigned char *)json, blen);
  obfuscate((unsigned char *)json, blen);
  memcpy(buf, "DGDATA", 6);
  {
    /* Via a temporary, NOT snprintf straight into buf+6: snprintf appends a
     * NUL, and the header is exactly 14 bytes, so writing 8 digits at offset 6
     * would put that NUL at offset 14 -- the first byte of the payload. That
     * corrupts one character of JSON in a way the checksum then certifies as
     * correct, which is the worst kind of bug to find later. */
    char hex[9];
    snprintf(hex, sizeof hex, "%08x", computed);
    memcpy(buf + 6, hex, 8);
  }

  f = fopen(path, "wb");
  if (!f) { debugPrintf("savetool: cannot write %s\n", name); return; }
  fwrite(buf, 1, 14 + blen, f);
  fclose(f);
  debugPrintf("savetool: patched %s (%d value(s))\n", name, edits);
}

/* ------------------------------------------------------------------ */

void smk_savetool_apply(void) {
  DIR *d;
  struct dirent *e;
  int files = 0;

  if (!read_settings()) return;     /* no saves.txt, or nothing uncommented */

  d = opendir(g_data_root);
  if (!d) { debugPrintf("savetool: cannot open %s\n", g_data_root); return; }

  while ((e = readdir(d)) != NULL) {
    const char *nm = e->d_name;
    size_t l = strlen(nm);
    char path[700];
    if (l < 6 || strcmp(nm + l - 5, ".save") != 0) continue;
    snprintf(path, sizeof path, "%s/%s", g_data_root, nm);
    patch_file(path, nm);
    files++;
  }
  closedir(d);
  debugPrintf("savetool: scanned %d .save file(s)\n", files);
}
