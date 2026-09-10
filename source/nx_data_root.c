/* nx_data_root.c -- see nx_data_root.h for the why and the resolution order.
 *
 * Adopted from the daggerfall_nx / clonehero_nx nx_data_root.c; steps 4 and 5 (scan sdmc:/ one
 * and two levels deep) are added here so the folder can live anywhere on the
 * card, not only under sdmc:/switch/.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "config.h"
#include "nx_data_root.h"

char g_data_root[768];
char g_log_path[832];
char g_data_root_how[640];

#ifndef DATA_ROOT_DEFAULT
#define DATA_ROOT_DEFAULT "sdmc:/switch/" GAME_FOLDER
#endif
#define SWITCH_DIR  "sdmc:/switch"
#define SD_ROOT     "sdmc:/"

/* Bounds on the scan. A card can have a lot of folders; these keep a bad
 * layout from turning into a long stall before the error appears. */
#define MAX_ENTRIES_PER_DIR 512
#define MAX_SUBDIRS_DEEP    128
/* How many directory levels below sdmc:/ to search. 3 covers sdmc:/switch/x,
 * sdmc:/games/x and sdmc:/homebrew/games/x. Deeper than that and the scan
 * costs more than it is worth -- put the .nro next to the game files and
 * argv[0] finds it wherever it lives, at any depth. */
#define SCAN_MAX_DEPTH      3

/* A directory counts as the game folder only if libnative.so is in it. Cheap,
 * and it is exactly the file whose absence started this. */
static int looks_like_root(const char *dir) {
  char p[640];
  struct stat st;
  if (!dir || !*dir) return 0;
  snprintf(p, sizeof p, "%s/libnative.so", dir);
  return stat(p, &st) == 0 && st.st_size > 0;
}

static int is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void adopt(const char *dir, const char *how) {
  snprintf(g_data_root, sizeof g_data_root, "%s", dir);
  snprintf(g_log_path,  sizeof g_log_path,  "%s/smk_nx.log", g_data_root);
  snprintf(g_data_root_how, sizeof g_data_root_how, "%s", how);
}

/* Scan one directory level for a child that looks like the game root. */
static int scan_level(const char *base, char *out, size_t outsz, char *how, size_t howsz) {
  DIR *d = opendir(base);
  if (!d) return 0;
  struct dirent *de;
  int looked = 0;
  while ((de = readdir(d)) != NULL && looked < MAX_ENTRIES_PER_DIR) {
    if (de->d_name[0] == '.') continue;
    looked++;
    char cand[512];
    /* base may or may not end in '/' (SD_ROOT does, SWITCH_DIR does not).
     * Joining blindly gives "sdmc://name", which then leaks into g_data_root
     * and into every strncmp() that compares a path against it. */
    size_t bl = strlen(base);
    int trailing = (bl && base[bl - 1] == '/');
    snprintf(cand, sizeof cand, trailing ? "%s%s" : "%s/%s", base, de->d_name);
    if (looks_like_root(cand)) {
      snprintf(out, outsz, "%s", cand);
      snprintf(how, howsz, "found by scanning %s (folder '%s')", base, de->d_name);
      closedir(d);
      return 1;
    }
  }
  closedir(d);
  return 0;
}

/* Depth-limited search for a folder containing the game. Level 1 is the
 * immediate children of `base`. Returns 1 and fills `out`/`how` on the first
 * hit, which makes the result depend on directory order -- acceptable, because
 * a card with two copies of the game is already ambiguous and argv[0] settles
 * it whenever the launcher provides one. */
static int descend(const char *base, int level,
                   char *out, size_t outsz, char *how, size_t howsz) {
  DIR *d;
  struct dirent *de;
  int looked = 0;

  if (level > SCAN_MAX_DEPTH) return 0;

  /* Check this level's children first: shallower matches win, which keeps the
   * common layouts fast and predictable. */
  if (scan_level(base, out, outsz, how, howsz)) return 1;

  d = opendir(base);
  if (!d) return 0;
  while ((de = readdir(d)) != NULL && looked < MAX_SUBDIRS_DEEP) {
    char sub[512];
    size_t bl = strlen(base);
    if (de->d_name[0] == '.') continue;
    /* Skip what cannot be it, or is ruinously large to walk. */
    if (level == 1) {
      if (!strcasecmp(de->d_name, "Nintendo"))   continue;   /* huge */
      if (!strcasecmp(de->d_name, "emuMMC"))     continue;   /* huge */
      if (!strcasecmp(de->d_name, "atmosphere")) continue;
      if (!strcasecmp(de->d_name, "bootloader")) continue;
    }
    snprintf(sub, sizeof sub, "%s%s%s", base,
             (bl && base[bl - 1] == '/') ? "" : "/", de->d_name);
    if (!is_dir(sub)) continue;
    looked++;
    if (descend(sub, level + 1, out, outsz, how, howsz)) { closedir(d); return 1; }
  }
  closedir(d);
  return 0;
}

void nx_resolve_data_root(int argc, char *argv[]) {
  char cand[512], how[512];

  /* ---- 1. the directory the .nro was launched from ---------------------- */
  if (argc >= 1 && argv && argv[0] && argv[0][0]) {
    const char *a0 = argv[0];
    /* hbmenu normally gives "sdmc:/switch/<dir>/<name>.nro". Some launchers
     * hand over a bare "/switch/..." with no device prefix; normalise that. */
    if (!strchr(a0, ':') && a0[0] == '/')
      snprintf(cand, sizeof cand, "sdmc:%s", a0);
    else
      snprintf(cand, sizeof cand, "%s", a0);

    char *slash = strrchr(cand, '/');
    if (slash && slash != cand) {
      *slash = '\0';                       /* strip "/<name>.nro"            */
      if (looks_like_root(cand)) {
        adopt(cand, "from argv[0] (.nro location)");
        return;
      }
    }
  }

  /* ---- 2. compile-time default ------------------------------------------ */
  if (looks_like_root(DATA_ROOT_DEFAULT)) {
    adopt(DATA_ROOT_DEFAULT, "compile-time default");
    return;
  }

  /* ---- 3. one level under sdmc:/switch/ --------------------------------- */
  if (scan_level(SWITCH_DIR, cand, sizeof cand, how, sizeof how)) {
    adopt(cand, how);
    return;
  }

  /* ---- 4. one level under sdmc:/ ---------------------------------------- */
  if (scan_level(SD_ROOT, cand, sizeof cand, how, sizeof how)) {
    adopt(cand, how);
    return;
  }

  /* ---- 5. deeper under sdmc:/ ------------------------------------------- *
   * For cards organised as sdmc:/games/bsm2/ or sdmc:/homebrew/games/bsm2/.
   * Descends up to SCAN_MAX_DEPTH levels, skipping the folders that are either
   * already covered or are known to be enormous, and stopping after
   * MAX_SUBDIRS_DEEP directories at each level so a full card cannot stall
   * boot. argv[0] already covers the normal case; this is the fallback for
   * launches that do not provide it (title override, for instance). */
  if (descend(SD_ROOT, 1, cand, sizeof cand, how, sizeof how)) {
    adopt(cand, how);
    return;
  }

  /* ---- 6. nothing validated; keep the default so errors read sensibly ---- */
  adopt(DATA_ROOT_DEFAULT,
        "NOT FOUND -- no folder on the card contains libnative.so; "
        "falling back to the compile-time default");
}

/* mkdir -p for the resolved root. Walks the path creating each component,
 * skipping the "sdmc:" mount prefix and the first component below it, because
 * mkdir on a devoptab root or a bare top-level name makes newlib's _stat_r
 * null-deref its devoptab entry (the same trap libc_shim.c guards). */
void nx_make_root_dirs(void) {
  char tmp[768];
  char *p;
  size_t i = 0;

  snprintf(tmp, sizeof tmp, "%s", g_data_root);

  /* step past "sdmc:" */
  p = strchr(tmp, ':');
  i = p ? (size_t)(p - tmp) + 1 : 0;
  if (tmp[i] == '/') i++;               /* and the slash after it */

  for (; tmp[i]; i++) {
    if (tmp[i] != '/') continue;
    tmp[i] = 0;
    mkdir(tmp, 0777);                   /* ignore EEXIST and the root cases */
    tmp[i] = '/';
  }
  mkdir(tmp, 0777);
}

const char *nx_path(const char *sub) {
  static char buf[8][768];
  static unsigned n = 0;
  char *b = buf[n++ & 7];
  snprintf(b, sizeof buf[0], "%s%s", g_data_root, sub ? sub : "");
  return b;
}

/* Supermonkey needs its Assets/ tree. The packed archive Assets/data.jet is
 * the single best sentinel: it is always present in a complete copy, it is
 * named the same on every build, and its absence is by far the most common
 * staging mistake after the root itself (people copy lib/ but not assets/).
 * Checked at boot and logged, so the question is answered before it is asked.
 *
 * Both layouts are accepted: the archive may sit at <root>/Assets/data.jet if
 * the Assets folder was dropped straight next to the .nro, or at
 * <root>/assets/Assets/data.jet if the APK's assets/ folder was copied whole.
 * These mirror the first two entries of ASSET_ROOTS in config.h. */
int nx_have_assets(void) {
  static const char *names[] = {
    "/" JET_ARCHIVE,            /* <root>/Assets/data.jet         */
    "/assets/" JET_ARCHIVE,     /* <root>/assets/Assets/data.jet  */
  };
  struct stat st;
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (stat(nx_path(names[i]), &st) == 0 && S_ISREG(st.st_mode)) return 1;
  return 0;
}
