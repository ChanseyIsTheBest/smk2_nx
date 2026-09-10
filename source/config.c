/* config.c -- read/write config.txt.
 *
 * config.h has declared read_config() / write_config() since the donor, but no
 * donor version ever defined them: BTD5's shipped main.c does not call them, so
 * the missing definitions never surfaced as a link error. This port's main.c
 * does call them, so here they are.
 *
 * The file is deliberately tiny and forgiving. It is edited by hand on an SD
 * card, often on a phone, so: one `key = value` per line, `#` comments, blank
 * lines ignored, unknown keys ignored rather than rejected, and any value that
 * fails to parse leaves the compiled-in default in place. A malformed
 * config.txt should never stop the game booting -- at worst it should be
 * ignored and said so in the log.
 *
 * MIT license -- see LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "util.h"

/* Trim ASCII whitespace in place; returns the first non-space character. */
static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  if (!*s) return s;
  char *e = s + strlen(s) - 1;
  while (e > s && isspace((unsigned char)*e)) *e-- = 0;
  return s;
}

static void apply(const char *key, const char *val) {
  if (!strcmp(key, "language")) {
    snprintf(config.language, sizeof config.language, "%s", val);
  } else if (!strcmp(key, "portrait")) {
    /* The documented values are 0/1/2. Words are accepted too, because they
     * are easier to remember than a number. */
    if      (!strcmp(val, "0") || !strcmp(val, "off") || !strcmp(val, "none")
                               || !strcmp(val, "fit") || !strcmp(val, "pillar")) config.portrait = 0;
    else if (!strcmp(val, "1") || !strcmp(val, "cw")  || !strcmp(val, "clockwise")) config.portrait = 1;
    else if (!strcmp(val, "2") || !strcmp(val, "ccw") || !strcmp(val, "anticlockwise")
                               || !strcmp(val, "counterclockwise")) config.portrait = 2;
    else if (!strcmp(val, "3") || !strcmp(val, "stretch")) config.portrait = 3;
    else debugPrintf("config: portrait value not understood, keeping %d\n", config.portrait);
  } else if (!strcmp(key, "cw") || !strcmp(key, "ccw") || !strcmp(key, "fit")
             || !strcmp(key, "stretch")) {
    /* Somebody uncommented a note instead of editing the setting -- an easy
     * mistake to make, and it used to fail silently with nothing but "ignoring
     * unknown key" in the log. The intent is unambiguous, so honour it and say
     * plainly what happened. */
    config.portrait = !strcmp(key, "cw") ? 1 : !strcmp(key, "ccw") ? 2
                    : !strcmp(key, "fit") ? 0 : 3;
    debugPrintf("config: read '%s' as 'portrait %d'. That line is a NOTE, not a\n"
                "        setting -- put the value on the 'portrait' line instead.\n",
                key, config.portrait);
  } else {
    debugPrintf("config: ignoring unknown key '%s'\n", key);
  }
}

int read_config(const char *file) {
  /* Defaults first, so a missing or partial file still leaves a sane struct.
   * "auto" means "use the built-in default" -- ninjakiwi.c treats it as such
   * when the engine asks for the device language. */
  snprintf(config.language, sizeof config.language, "auto");
  config.portrait = PORTRAIT_DEFAULT;

  FILE *f = fopen(file, "r");
  if (!f) {
    /* Write it out with the defaults, so there is something to edit. Otherwise
     * every option is invisible: you have to read the source to learn that
     * portrait_mode exists, and guess the spelling. Costs one file write on
     * first run. */
    debugPrintf("config: %s not present -- writing defaults\n", file);
    if (write_config(file) == 0) debugPrintf("config: created %s\n", file);
    else                         debugPrintf("config: could NOT create %s\n", file);
    return 0;                    /* not an error: the file is optional */
  }

  /* Format: `name value`, whitespace separated. '#' starts a comment.
   * Deliberately not `name = value`: this file is edited by hand on an SD card,
   * often on a phone, and one less bit of punctuation to get wrong is worth
   * more than looking like an INI. An '=' between the two is tolerated anyway,
   * because people will type it. */
  char line[256];
  int n = 0;
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    char *hash = strchr(p, '#');
    if (hash) *hash = 0;
    p = trim(p);
    if (!*p) continue;

    /* split on the first run of whitespace (or '=') */
    char *v = p;
    while (*v && !isspace((unsigned char)*v) && *v != '=') v++;
    if (!*v) { debugPrintf("config: skipping valueless line: %s\n", p); continue; }
    *v++ = 0;
    while (*v && (isspace((unsigned char)*v) || *v == '=')) v++;
    char *k = trim(p);
    v = trim(v);
    if (*k && *v) { apply(k, v); n++; }
  }
  fclose(f);


  debugPrintf("config: read %d setting(s) from %s (language=%s, portrait=%d %s)\n",
              n, file, config.language, config.portrait,
              config.portrait == 0 ? "no rotation, 16:9 pillar" :
              config.portrait == 1 ? "rotated clockwise, right Joy-Con up" :
              config.portrait == 2 ? "rotated anticlockwise, left Joy-Con up"
                                   : "stretched");
  return n;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (!f) return -1;
  fprintf(f,
    "# Bloons Supermonkey (Switch) settings.\n"
    "# Options are 'name value' lines (whitespace-separated); '#' starts a\n"
    "# comment. Every '#' line below is a NOTE -- to change a setting, edit the\n"
    "# value on the setting's own line. Do not uncomment a note.\n"
    "\n"
    "# portrait -- the render is rotated 90 degrees to fill the screen (hold the\n"
    "# console rotated to play): 1 (default) rotates clockwise (right Joy-Con\n"
    "# up); 2 rotates the other way (left Joy-Con up); 0 disables rotation,\n"
    "# 16:9 pillar. Set 'portrait 0' if you play docked on a TV and don't want a\n"
    "# sideways picture.\n"
    "portrait %d\n"
    "\n"
    "# Two-letter language code, or 'auto' to let the port choose (English).\n"
    "language %s\n",
    config.portrait,
    config.language[0] ? config.language : "auto");
  fclose(f);
  return 0;
}
