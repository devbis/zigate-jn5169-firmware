/****************************************************************************
 * zigate_apdu_diag.h — APDU-pool usage diagnostics (OpenLumi call signatures).
 *
 * OpenLumi reported APDU pool usage to the host (status / diagnostic messages)
 * via u8GetApduUse()/u8GetMaxApdu(). Those symbols were *used but never
 * declared or defined* in the OpenLumi application, which linked against a
 * modified PDUM; every call site therefore relied on a C89 implicit
 * declaration (`int f()`). Stock JN-SW-4170 v2395 `pdum_gen.c` already provides
 * the accurate primitives PDUM_u16APduGetCrtUse()/PDUM_u16APduGetMaxUse() over
 * the generated APDU pool handle (apduZDP); zigate_compat.c defines these two
 * wrappers on top of them with the OpenLumi signatures.
 *
 *   u8GetApduUse()  -> APDU instances currently allocated (current use)
 *   u8GetMaxApdu()  -> APDU instances ever used (high-water mark)
 *
 * WHY THIS IS A SEPARATE HEADER.
 * These prototypes are the single source of truth for both symbols and are
 * consumed by host-diagnostic translation units (custom_diag.c,
 * app_general_events_handler.c) that have no reason to pull in the ZCL cluster
 * world. zigate_compat.h drags in zcl_options.h, zcl.h, ZclTime.h,
 * WindowCovering.h, control_bridge.h and MultistateInputBasic.h; including it
 * from those files just to obtain two `uint8 (void)` prototypes would couple
 * the diagnostics path to the whole cluster layer and risk macro collisions.
 *
 * This header therefore depends on **jendefs.h only** (for uint8/PUBLIC) and
 * includes nothing else, so it cannot participate in an include cycle.
 * zigate_compat.h includes it rather than restating the prototypes, so there
 * is exactly one declaration of each symbol in the tree.
 *
 * Implicit declarations of these two functions are FORBIDDEN: the app objects
 * that call them are compiled with -Werror=implicit-function-declaration (see
 * app/Build/ZigbeeNodeControlBridge/Makefile), and scripts/check.sh asserts
 * both the declaration/include wiring and that build flag.
 ****************************************************************************/
#ifndef ZIGATE_APDU_DIAG_H
#define ZIGATE_APDU_DIAG_H

#include <jendefs.h>

PUBLIC uint8 u8GetApduUse(void);
PUBLIC uint8 u8GetMaxApdu(void);

#endif /* ZIGATE_APDU_DIAG_H */
