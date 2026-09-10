/* smk_savetool.h -- edit the game's own saves at boot. MIT licensed.
 *
 * Reads saves.txt next to the .nro and writes the values it names into the
 * game's save files, in the game's own obfuscated+checksummed format, before
 * the engine loads them. Generates a fully commented saves.txt on first run
 * with everything switched off, so the file itself documents what can be
 * changed.
 *
 * Call once from main(), after nx_resolve_data_root() and before so_load().
 *
 * Modelled on papapear_nx's pps_savetool (MIT, same lineage). The mechanism
 * differs -- that game XORs against its own filename and has no checksum;
 * this one uses a positional additive shift plus a CRC that has to be
 * recomputed -- but the shape is deliberately the same.
 */
#ifndef __SMK_SAVETOOL_H__
#define __SMK_SAVETOOL_H__

void smk_savetool_apply(void);

#endif
