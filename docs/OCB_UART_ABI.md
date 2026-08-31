# Typed Open Coordinator Backup UART extension

Status: the default `OCB_TYPED_SUPPORT=1` implementation is an **export-only
metadata subset; not BackupCapable**. OCB ABI version 1, schema version 1. It
is additive to ZiGate diagnostic protocol 1.2 / build revision 17 and does not
change stock command layouts.

All multi-byte integers are unsigned **big-endian**, matching the existing
`ZNC_BUF_*` custom protocol. ZiGate framing and the trailing LQI byte are not
included in the payload lengths below. Every accepted request first receives
the stock correlated-by-command `0x8000` status frame, followed by its typed
response.

## Security boundary

Production/default builds do not dispatch legacy raw-PDM commands
`0x0B00`/`0x8B00`, `0x0B01`/`0x8B01`, or `0x0B02`.
`INSECURE_DEV_RAW_PDM=1` is an explicit default-off bench option, and the
Makefile rejects it when `OCB_TYPED_SUPPORT=1`.

The default metadata implementation exports coordinator and Trust Center IEEE
addresses plus PAN/network identifiers and counters. It exports no network
key, APS link key, PDM record, arbitrary address lookup, memory range, or
serialized C structure. Link-key requests return `FIELD_UNAVAILABLE` and no
key bytes. Restore is not implemented or advertised. Consequently the
capability is named `OCB_METADATA_EXPORT`, not full OCB/BackupCapable.

Authenticated encryption and a board-specific physical-presence unlock were
not added to the default metadata subset, and the target has no qualified
button input. The separately compiled experimental ABI deliberately treats
direct local UART possession as trust, but its public nonce relation is not
physical presence or authentication. The default build's residual local-serial
threat is disclosure of network identifiers and live counters, not key
material. The clean rev17 default build leaves a 196-byte linker RAM margin;
it has not been hardware-qualified.

## Compile-time gates and diagnostic capability bits

The build wrapper and Makefile defaults are:

```
OCB_TYPED_SUPPORT=1
OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=0
INSECURE_DEV_RAW_PDM=0
```

`OCB_TYPED_SUPPORT=1` dispatches `0x0D18`…`0x0D1C` and advertises diagnostic
capability bit 15. `OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1` requires typed OCB,
compiles `ocb_experimental.c`, dispatches `0x0D20`…`0x0D2A`, and adds
diagnostic capability bit 16. It does not add bit 17. Raw PDM is incompatible
with either OCB mode. Reserved diagnostic bit 17 is the production-qualified
BackupCapable bit and is always clear.

With the wrapper defaults (`GP_SUPPORT=1`), the complete diagnostic capability
bitmap is `0x00000000000CC60F` and `DIAG_FW_BUILD_ID=0x010DC53D`.
Experimental export changes these to `0x00000000000DC60F` and
`0x010CC53D`. Bit 18 independently advertises reset summary `0x0D2B`; bit 19
independently advertises reset context `0x0D2C`. These are
negotiation identifiers, not qualification claims.

## Common fields

Except `EXPORT_BEGIN`, requests begin with:

```
abi_version:u8 | schema_version:u8 | transaction_id:u32 | session_id:u32
```

Responses begin with the same fields plus `status:u8`. Transaction and session
IDs are echoed exactly. `EXPORT_BEGIN` carries only versions and transaction
ID; firmware allocates a non-zero random session ID. One bounded snapshot can
be active at a time. The session is bound to both the `EXPORT_BEGIN`
transaction ID and allocated session ID, so all CORE/LINK_KEY/END/STATUS
requests for it must repeat both values. A new valid `EXPORT_BEGIN` atomically
supersedes a stale export-only snapshot, so loss of a host session cannot wedge
the ABI. `END` wipes the snapshot.

Statuses: `0 OK`, `1 BAD_VERSION`, `2 BAD_LENGTH` (reserved; exact-length
failures use outer `0x8000 INCORRECT_PARAMETERS` and emit no typed response),
`3 NO_SESSION`, `4 SESSION_MISMATCH`, `5 FIELD_UNAVAILABLE`, `6 BUSY`
(reserved/currently not emitted).

## Commands

| Request / response | Payload |
|---|---|
| `0x0D18` / `0x8D18` EXPORT_BEGIN | req 6: `ver, schema, txn`; rsp 19: common response + `capabilities:u32, fields:u32` |
| `0x0D19` / `0x8D19` EXPORT_CORE | req 10 common; rsp 55: common response + 44-byte core |
| `0x0D1A` / `0x8D1A` LINK_KEY_BY_EUI | req 18 common + `eui64`; rsp 24 common + `unavailable_field:u32, eui64, key_length:u8(0)` |
| `0x0D1B` / `0x8D1B` EXPORT_END | req 10 common; rsp 16 common + `record_count:u8(1), digest:u32` |
| `0x0D1C` / `0x8D1C` STATUS | req 10 common; rsp 20 common + `active:u8, fields:u32, digest:u32` |

OCB-local capabilities are `bit0 EXPORT_CORE`, `bit1 STATUS_DIGEST`.
`bit2 LINK_KEYS`, `bit3 RESTORE`, and `bit4 PHYSICAL_UNLOCK` are clear. These
OCB-local bits are fields in `0x8D18` and are distinct from the 64-bit
diagnostic capability bitmap negotiated by `0x8D0F`.

The CORE body is:

```
fields:u32
coordinator_ieee:u64
pan_id:u16
extended_pan_id:u64
channel:u8
channel_mask:u32
nwk_update_id:u8
security_level:u8
nwk_key_sequence:u8
authoritative_nwk_outgoing_counter:u32
aps_trust_center_ieee:u64
aps_flags:u8
aps_key_type:u8
```

`aps_flags`: bit0 designated coordinator, bit1 insecure join, bit2 decrypt
install code. A field is usable only when its validity bit is set. Field
validity bits are: coordinator IEEE bit 0, PAN ID bit 1, extended PAN ID bit 2,
channel bit 3, channel mask bit 4, NWK update ID bit 5, security level bit 6,
NWK key sequence bit 7, NWK outgoing counter bit 8, APS Trust Center address
bit 9, and APS state bit 10. Network key, link keys, and APS per-peer counters
are explicitly represented by permanently clear bits 16, 17, and 18; no
values are synthesized.

`digest` is 32-bit FNV-1a over the exact 44-byte canonical CORE body beginning
with `fields`, before framing. It is a readback/integrity check, **not**
authentication or encryption.

The snapshot reads typed live v2395 handles only. Compile-time checks pin the
generated coordinator role, one channel-mask entry, and 16-byte network-key
width. Runtime checks require exactly one channel-mask entry before marking it
valid.

## Restore

The default typed metadata subset (`OCB_TYPED_SUPPORT=1`) still has no restore
ABI and no restore capability bit; it never mutates persistent state.

The experimental build (`OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1`) implements a
streamed, field-tagged restore over opcodes `0x0D24`…`0x0D28`, gated by the same
30-second CHALLENGE/UNLOCK as key export. It is a bench-only, unqualified path:
it is not authenticated, has no atomic rollback, and never sets the reserved
BackupCapable bit 17. Run it only against a factory-reset target — recognised
fields are written into the live NIB/AIB and take effect after the COMMIT reboot.

The restore stream is field-tagged so an image built by a different
firmware/PDM revision skips unknown field ids instead of corrupting layout.
`RESTORE_FIELD` carries `field_id:u16, length:u16, value[]`, and an unrecognised
`field_id` is acknowledged as SKIPPED. The restorable fields are the inverse of
the export: network key + key sequence + NWK outgoing frame counter, PAN id,
extended PAN id, channel, network short address, nwkUpdateId, Trust Center
address + default TC link key + key type, and per-device link keys
(`RESTORE_LINK`, one per message). HIL-verified byte-for-byte on real JN5169
hardware, surviving the COMMIT reboot: coordinator IEEE, PAN id, extended PAN
id, channel, network key, TC link key, **and the NWK outgoing frame counter**.

**NWK outgoing frame counter — how the restore actually works.** v2395's
`ZPS_vSaveAllZpsRecords()`/`ZPS_vNwkSaveSecMat()` never carry a directly-written
`sTbl.u32OutFC` into PDM, because the SDK does not persist that field as a
plain value at all. Root-caused by disassembling `libZPSNWK_JN516x.a`: at boot,
`ZPS_pvNwkRestoreFrameCounter()` reconstructs `sTbl.u32OutFC` as
`bitmap_value << shift`, where `bitmap_value` is `PDM_eGetBitmap()`'s output
for a dedicated PDM *bitmap* record (`PDM_ID_INTERNAL_NWK_OUT_FC_BITMAP`,
`0xf106`, undocumented in the public headers) — a pure popcount of
`PDM_eIncrementBitmap()` calls, confirmed via disassembly and an on-device
readback to be **independent of `PDM_eCreateBitmap()`'s "InitialValue"
parameter** (the SDK itself always creates this record with `InitialValue 0`,
in the private helper `vIncrementFrameCounterInPdm`). `shift` is
`ZPS_u32NwkFcSaveCountBitShift()`, generator-configured via `.zpscfg`'s
`NwkFcSaveCountBitShift` (`10` → block size 1024 in this build). RESTORE_FIELD
therefore deletes and recreates that bitmap, then writes `ceil(target/block)`
via `ePDM_SetBitmapToValue()` in one shot — an exported-but-undeclared symbol
in `libPDM_EEPROM_NO_RTOS_JN516x.a` found by disassembly, which writes the
bitmap's `{chain_count, remainder flag bytes}` record state directly instead
of looping `PDM_eIncrementBitmap()` `ceil(target/block)` times. That loop is
O(N) per call (linear scan for the next free flag byte, then a full
segment-header rewrite) — O(N²) overall — and HIL with a frame counter in the
millions took over 15 minutes and never completed; the one-shot write is O(1)
regardless of magnitude, which is why no step cap is needed and the host
script's default timeout could stay at 15s.

**Coordinator IEEE adoption (`field_id 0x000C`) works, after a root-caused
fix.** HIL testing first found `ZPS_vSetOverrideLocalIeeeAddr()` reliably
hung boot the instant it was actually invoked with a real value — reproduced
from both a factory-new boot and an already-networked `E_RUNNING` boot.
Disassembling `libZPSMAC_Mini_SOC_JN516x.a` / `libMiniMac_JN5169.a`
(`ba-elf-objdump -dr` on the extracted objects) traced the call chain:
`ZPS_vSetOverrideLocalIeeeAddr()` → `vAppApiSetMacAddrLocation()` →
`SOC_ZPS_vMacPibSetExtAddr()` → `vMiniMac_MLME_SetReq_PanId()` →
`vMMAC_SetRxAddress()` — a hardware MAC register write, not a deferred
"read this pointer later" hook as the name suggests. The boot hook called it
*before* `ZPS_eAplAfInit()`, hitting that register before the radio was
clocked/initialised. Moving the call (in `app_start.c`, gated to this build)
to run *after* every `ZPS_eAplAfInit()` call fixed it: HIL-verified 3/3
reliable restore→COMMIT→reboot→verify cycles, including a correct
byte-for-byte network state afterward. What has **not** been tested: every
run so far is a self-restore on one physical unit (source IEEE == target
IEEE) — a true two-device migration with a genuinely different target IEEE,
and behavior with real paired end devices, remain unverified. See
`OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE` for the full caveat, which stays set as a
standing caution flag (mutating the live MAC address is inherently risky)
rather than a "broken" marker.

Per-flash-TCLK APS frame counters remain unrestorable — v2395 exposes no
API — so a restored key starts with a zero APS counter that re-syncs on the next
exchange (`OCBEXP_LIMIT_FLASH_TCLK_COUNTERS`). COMMIT persists via
`ZPS_vSaveAllZpsRecords()` and reboots; there is no rollback, so keep the unit
powered throughout it.

**Boot reliability on erased/factory-new PDM**: this is a *separate* issue from
the IEEE-adoption hang above. An early build of this experimental image showed
an unreliable boot on a truly erased-PDM/factory-new target regardless of IEEE
adoption. A settle delay at the top of `vInitialiseApp()`
(`vOCBExpBootSettleDelay()`, still present, gated to this build) empirically
fixed it: dozens of consecutive erase-PDM boot cycles have since succeeded in
HIL testing, plus multiple full backup→restore→reboot→verify round trips
including directly onto a never-networked target.

Unlike the IEEE-adoption hang, this one is not *proven* via disassembly (no
JTAG or logic analyzer was available to observe the actual hardware signals),
but disassembling `libPDM_EEPROM_NO_RTOS_JN516x.a` and
`libHardwareApi_JN5169.a` narrowed it to a specific, evidence-backed
mechanism rather than a vague guess: `PDM_eInitialise()`, on truly blank PDM
(always the case in this HIL scenario), calls
`iPDM_CreateFileSystemInRAM()` → `vPDM_GenerateCleanFileSystem()`, which loops
`iAHI_EraseEEPROMsegment()` over every flash segment. Each call runs
`bAHI_InitialiseEEPROM()`, which writes an MMIO "enable" pattern (`0x10100`)
to register `0x1000090` — but *only* on this boot's very first EEPROM
operation, gated by a global counter (`u8EEPROMinitialiseCount == 0`); every
later call in the same erase loop writes `0` there instead. The erase command
that follows (via registers `0x1000080`/`0x1000084`) is paced by a fixed
16-iteration dummy-write loop, not a hardware-ready poll. If that one-time
enable races the EEPROM/flash controller's own post-reset settle time, and the
fixed 16-cycle pad doesn't cover it, a command issued into the controller's
internal reset window is a plausible way to wedge it — consistent with a hang
inside a per-segment erase loop that clears only on a real hardware reset.
`vOCBExpBootSettleDelay()` sits exactly where this theory says it needs to:
before the very first PDM/EEPROM hardware access this boot performs. Treat
this mechanism as evidence-backed, not confirmed, and this delay as a
characterized empirical workaround rather than a datasheet-verified fix;
re-verify after any source change that alters this binary's size or timing.

## Experimental trusted-local-serial key export

`OCB_KEY_EXPORT_RESTORE_EXPERIMENTAL=1` builds an additional ABI that follows
the Z-Stack/EZSP trust model: possession of the direct local UART is treated as
physical trust. Authorization and destructive confirmation belong in the host.
The flag is default-off and cannot be combined with `INSECURE_DEV_RAW_PDM`.

There is **no cryptographic authentication or confidentiality**. The unlock is
a 30-second accidental-invocation guard:

1. Send CHALLENGE with a transaction ID.
2. Firmware returns a random nonce.
3. Send UNLOCK with
   `confirmation = nonce XOR transaction_id XOR 0x4f434221`.

The confirmation relation and constant are public. They are not a password,
MAC, signature, or proof of possession. AES was deliberately not applied:
without an independently provisioned secret or authenticated key agreement,
AES would only obscure bytes while providing no meaningful session security.

All experimental requests start with:

```
abi_version:u8 | schema_version:u8 | transaction_id:u32
```

All responses start with:

```
abi_version:u8 | schema_version:u8 | transaction_id:u32 | status:u8
```

All integers are big-endian.

Every exact-length request first receives outer `0x8000 SUCCESS`, followed by
the typed response. A wrong-length request receives only outer `0x8000
INCORRECT_PARAMETERS`. CHALLENGE binds the nonce to its transaction ID;
UNLOCK and all operations requiring unlock must use that same transaction ID.
A new CHALLENGE or failed UNLOCK clears prior unlock state. The 30-second timer
is checked against the JN5169 tick timer.

| Request / response | Exact payload |
|---|---|
| `0x0D20` / `0x8D20` CHALLENGE | req 6; rsp 16: prefix + `nonce:u32, ttl_seconds:u8, limitations:u32` |
| `0x0D21` / `0x8D21` UNLOCK | req 14: common + `nonce:u32, confirmation:u32`; rsp 12: prefix + `ttl:u8, limitations:u32` |
| `0x0D22` / `0x8D22` SECRET_CORE | req 6; rsp 61: prefix + `available:u32, limitations:u32, nwk_seq:u8, nwk_key[16], nwk_out:u32, tc_type:u8, tc_key[16], tc_out:u32, tc_in:u32` |
| `0x0D23` / `0x8D23` LINK_KEY | req 8: common + `kind:u8, index:u8`; rsp 46: prefix + `kind:u8, index:u8, eui64:u64, available:u32, key_type:u8, key[16], aps_out:u32, aps_in:u32` |
| `0x0D24` / `0x8D24` RESTORE_BEGIN | req 6; rsp 15: prefix + `restore_caps:u32, limitations:u32`. Starts a restore session. |
| `0x0D25` / `0x8D25` RESTORE_FIELD | req ≥10: `abi, schema, txn` + `field_id:u16, length:u16, value[length]`; rsp 10: prefix + `field_id:u16, result:u8` (`0` applied, `1` skipped-unknown, `2` bad-length, `3` unavailable). Repeatable. |
| `0x0D26` / `0x8D26` RESTORE_LINK | req 31: `abi, schema, txn` + `eui64, key_type:u8, key[16]`; rsp 17: prefix + `eui64, result:u8, aps_counter_lost:u8(1)`. Repeatable, one per device. |
| `0x0D27` / `0x8D27` VALIDATE | req 6; rsp 12: prefix + `present:u32, mandatory_ok:u8`. No writes. |
| `0x0D28` / `0x8D28` COMMIT | req 6; rsp 11: prefix + `present:u32`. On OK persists and reboots (reply emitted first). |
| `0x0D29` / `0x8D29` STATUS | req 6; rsp 13: prefix + `unlocked:u8, ttl:u8, limitations:u32` |
| `0x0D2A` / `0x8D2A` ABORT | req 6; rsp 11: prefix + `limitations:u32`; immediately wipes unlock state |

Experimental typed statuses are `0 OK`, `1 BAD_VERSION`, `2 LOCKED`,
`3 NOT_FOUND`, `4 LAYOUT_MISMATCH`, `5 NO_SESSION` (a restore op with no prior
RESTORE_BEGIN), `6 INCOMPLETE` (VALIDATE/COMMIT before all mandatory fields are
present), and `7 BAD_FIELD`. Status `5` was `RESTORE_UNSUPPORTED` while the
restore opcodes were stubs; values `0`…`4` keep their export-path meaning.

Availability bits are: network key bit 0, NWK outgoing counter bit 1, TC/APS
link-key bytes bit 2, APS outgoing counter bit 3, APS incoming counter bit 4,
and EUI bit 5. Callers must use these bits; zero-filled unavailable fields are
not evidence that a key or counter exists.

LINK_KEY kinds are `0 default TC`, `1 live APS key-table index`, and `2 flash
TCLK index`. The generated target has one live APS key-table entry and 70 flash
TCLK slots. Missing entries return NOT_FOUND. Flash TCLK key bytes and EUI are
available, but their APS frame counters are not exposed by v2395, so the
counter availability bits remain clear.

Every temporary key array and the complete experimental TX buffer are wiped
through volatile stores after `vSL_WriteMessage()` returns.

### Capability truth

Diagnostic capability bit 16 means **experimental trusted-serial key export and
streamed restore**. Reserved bit 17 is the production-qualified BackupCapable bit
and is never included in `DIAG_CAP_BITMAP`; implementing restore does not set it,
because the path is unauthenticated, non-atomic, and unqualified. The restore
capabilities available in a session are reported positively in the RESTORE_BEGIN
response (`restore_caps:u32`). Experimental builds report limitation bits for:

- bit 0: no authentication or encryption;
- bit 1: unavailable flash-TCLK counters;
- bit 2: no atomic rollback;
- bit 3: unqualified restore;
- bit 4: unsafe/unqualified coordinator IEEE override.

### v2395 symbol and layout evidence

The linked `libZPSAPL_LEGACY_JN516x.a` exports:

- `ZPS_vGetRestorePoint()` / `ZPS_vSetRestorePoint()`;
- `ZPS_vSetFixedNwkKey()`, `ZPS_vSetKeys()`, `zps_vSaveAllZpsRecords()`;
- `ZPS_bIsLinkKeyPresent()` and `zps_psFindKeyDescr()`;
- `ZPS_u64GetFlashMappedIeeeAddress()`;
- private-header-omitted `zps_bGetFlashCredential()`,
  `zps_eAddCredToFlash()`, and `zps_bAreCredPresent()`.

`zps_bGetFlashCredential()` is isolated behind the experimental flag using the
same exact declaration already present in `app_Znc_cmds.c`; no new guessed ABI
is introduced.

Generated `zps_gen.c` establishes:

- `s_asNwkSecMatSet[2]`;
- `s_keyPairTableStorage[4]`, with runtime key-table size 1;
- default TC key at storage slot 1;
- `au32IncomingFrameCounter[4]`;
- trust-center device table size 36;
- MAC table size 36;
- flash TCLK capacity configured by the application as 70.

Compile-time checks pin the 16-byte key widths and 24-byte legacy APS key
descriptor. Runtime checks require security-material count 2, APS key-table
size 1, MAC table size 36, all required pointers non-null, and the generated
legacy configuration before any key is copied.

The experimental restore applies fields through typed ZPS setters and direct
NIB/AIB writes (the inverse of the verified export), then
`ZPS_vSaveAllZpsRecords()` + reboot — not the raw restore-point/serialized-PDM
path, which is layout-fragile across versions. The NWK outgoing frame counter
is the one field that needed a different mechanism than a direct struct write
(see above); it restores correctly via the PDM bitmap it's actually persisted
through. A limit inherent to v2395 remains surfaced rather than hidden: there
is no supported per-flash-TCLK counter get/set API (up to 70 credentials in
the TCLK flash area), so a restored link key starts with a zero APS counter
that re-syncs on the next exchange; and no atomic rollback contract exists for
the multi-record COMMIT, so the unit must stay powered through it. Coordinator
IEEE adoption via `ZPS_vSetOverrideLocalIeeeAddr()` **works**: it hung boot
when the boot-time call ran before `ZPS_eAplAfInit()` (disassembly-root-caused
to a hardware MAC register write reached before the radio was up), and is
fixed by calling it after instead (see above and
`OCBEXP_LIMIT_IEEE_OVERRIDE_UNSAFE`).
