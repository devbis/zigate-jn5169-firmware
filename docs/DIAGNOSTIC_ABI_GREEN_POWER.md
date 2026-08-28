# Diagnostic ABI — negotiated Green Power proxy commissioning command

Status: **implemented in firmware C** against v2395 (the ControlBridge port
compiles and links — see `MIGRATION_STATUS.md`). This document is the normative
wire contract that the Go host (`adapter/zigate/greenpower.go`) codes against,
and that the firmware implements. The custom diagnostic protocol is at
**proto 1.2 / build rev 9** (`custom_diag.h`); rev9 changed no command encoding.

## Rationale

Before rev7 the host advertised Green Power commissioning through capability
bit `1 << 3` but had no firmware command behind it. It therefore built a ZGP
*Proxy Commissioning Mode* ZCL frame itself and pushed it through the stock
`0x0530` send-data command aimed at `0xFFFC`. Two things are wrong with that:

1. **It does not transmit.** The stock ZiGate `0x0530` encoding is an
   *acknowledged short-address unicast*. Sent to the `0xFFFC` broadcast
   address it cannot be acknowledged, and the physical rev6 HIL answered
   firmware status `0xA6` (`ZPS_APL_APS_E_NO_ACK`). The read-only capabilities
   passed on the same run; only this reversible operation failed.
2. **Even a successful broadcast would be insufficient.** The Proxy
   Commissioning Mode command tells *other* proxies what to do. It does not
   open, bound or close the coordinator's **own** proxy commissioning window,
   which lives in the JN5169 Green Power cluster state machine
   (`tsGP_GreenPowerCustomData.eGreenPowerDeviceMode` /
   `u16CommissionWindow`).

rev7 therefore adds an explicit, negotiated, bounded command that drives the
local Green Power state machine and lets the SDK close the window on expiry.

## rev8: per-request correlation

The rev7 encoding carried no correlation field. The stock ZiGate serial
protocol correlates a data response only by its *response type*, so a `0x8D17`
that answers a request the host has already given up on is byte-for-byte
indistinguishable from the answer to the next request. That is not a
theoretical concern for this command: it is the only custom command whose
response describes **mutable** coordinator state, so consuming a stale frame
means reporting the *previous* window (say, "commissioning, 30 s") as
confirmation of the operation just requested (say, a close). The host would
then either report success for a window that is still open, or fail a correct
operation.

rev8 therefore adds an explicit host-allocated `transaction_id` to the request
and echoes it in the response. Nothing else changes:

- protocol stays **1.2**; only the `0x0D17`/`0x8D17` pair is re-encoded
  (request 3 → 7 bytes, response 5 → 9 bytes);
- the command is gated behind capability bit `1 << 3`, which was first backed
  by a real handler in rev7, and hosts must re-negotiate `0x0D0F` (and read the
  build id) before issuing it, so no shipped host can be left speaking the
  rev7 encoding;
- the firmware never interprets the id. Every 32-bit value is legal, and the
  id is echoed on **every** response emitted for a structurally valid request —
  Green Power success and Green Power failure alike — so the host correlates
  failures exactly as it correlates successes.

### Why the id is 32 bits wide

The first rev8 draft used a single byte. rev8 has not been flashed or
published, so that ABI was corrected in place rather than deferred to a rev9.
Two properties are needed for the correlation to be sound, and a byte-wide id
allocated ahead of the host's serialized request lock has neither:

1. **The id must not age.** The host serializes complete transactions behind
   one lock. An id allocated *before* the caller queues for that lock is
   already stale by the time the request is written, so ids do not reach the
   wire in allocation order. The host now allocates the id only after
   acquiring the transaction lock, i.e. once it is the in-flight transaction.
2. **The id must not wrap onto an outstanding transaction.** With a byte-wide
   counter, 256 transactions — trivially reachable on a busy or retrying host —
   bring the id back to that of an earlier request whose late response may
   still be in flight, which is exactly the failure the field exists to
   prevent. A 32-bit id cannot wrap within the lifetime of a queued request.

The id is encoded big-endian, matching the `0x0D0F` capability nonce, which is
the existing convention for multi-byte custom fields.

### Status consistency

The firmware emits `status = 0` (OK) if and only if the underlying GP call
returned `E_ZCL_SUCCESS` (`gp_status == 0`), and `status = 1` (GP error) if and
only if it did not. The host enforces the same invariant when decoding: an
`OK` response carrying a non-zero `gp_status`, or a `GP_ERROR` response
carrying `gp_status == 0`, is rejected as malformed rather than reported as a
success or as a stack error with no error behind it.

## Endpoint mapping (why the source endpoint is 2, not 242)

The Green Power **APS** endpoint is 242 (`ZPG_GP_ENDPOIND_ID`), but this
firmware registers its GP cluster instance on the locally mapped ZCL endpoint
`GREENPOWER_END_POINT_ID = 2` (`zcl_options.h`). Every SDK GP call takes the
*mapped local* endpoint as the source and 242 as the destination. The host
must not attempt to reproduce this mapping; it is entirely firmware-internal.

## Why the SDK's own `eGP_ProxyCommissioningMode()` is only half usable

`eGP_ProxyCommissioningMode()` (`GreenPowerProxyCommissioningMode.c`) is the
supported SDK entry point, and the firmware calls it verbatim for the
**disable** direction.

Its **enable** direction is unreachable in this build. Before transmitting it
unconditionally performs

```c
eZCL_ReadLocalAttributeValue(ep, GREENPOWER_CLUSTER_ID, /*bIsServer=*/TRUE, ...,
                             E_CLD_GP_ATTR_ZGPS_COMMISSIONING_EXIT_MODE, ...)
```

`ZGPS_*` are **sink/server** attributes. Both the attribute definition table
(`asCLD_GreenPowerClusterAttributeDefinitionsServer[]` in `GreenPower.c`) and
the matching `tsCLD_GreenPower` fields are compiled only under
`GP_COMBO_BASIC_DEVICE`. This coordinator is a **`GP_PROXY_BASIC_DEVICE`**
(`app/Build/ZigbeeNodeControlBridge/Makefile`), registered through
`eGP_RegisterProxyBasicEndPoint()`, so that read always fails and the function
returns early **without changing state and without transmitting**. Adding the
sink attribute set would mean turning the coordinator into a Green Power sink,
which is a different device role and a different certification posture.

The firmware therefore uses the narrowest application-owned integration
(`u8App_GP_SetProxyCommissioningMode()` in `app_green_power.c`):

- it reproduces exactly the local state the SDK ENTER branch would have set, in
  `sGPDeviceInfo` — storage the **application** defines and owns; the SDK only
  holds a pointer to it, and this file already initialises other fields of it;
- it hands the frame to the SDK's own `eGP_ProxyCommissioningModeSend()`;
- it programs `u16CommissionWindow` in 20 ms ticks so the **SDK's own**
  scheduler enforces the bound: `eGP_Update20mS()` (`GreenPowerScheduler.c`,
  driven by the ZCL 1 ms timer registered in `GreenPower.c`) decrements it and
  calls `vGP_ExitCommMode()` at zero;
- it rolls the local state back if the send fails, so the proxy is never left
  commissioning on a frame that did not leave the node;
- it clears the countdown on an explicit disable as **defensive hygiene**. This
  is not a correctness fix: the SDK EXIT branch already sets
  `eGreenPowerDeviceMode = E_GP_OPERATING_MODE`
  (`GreenPowerProxyCommissioningMode.c:232-236`), and `eGP_Update20mS()` only
  calls `vGP_ExitCommMode()` when the countdown hits zero **and** the mode is
  not `E_GP_OPERATING_MODE` (`GreenPowerScheduler.c:108-116`), so no second
  exit broadcast is possible either way. Clearing it keeps disable
  deterministic, avoids decrementing a countdown for an already-closed window,
  and keeps `u16CommissionWindow != 0` a truthful "window open" indicator.

No SDK source is modified and no private SDK object is written.

## Capability negotiation

- Capability bit: **`1 << 3`** (`0x0000000000000008`) in the `0x8D0F`
  capability bitmap.
- The bit and the handler share a single compile switch,
  `DIAG_HAVE_GP_COMMISSIONING` (derived from `CLD_GREENPOWER` in
  `custom_diag.h`). It gates the prototype, the handler body, the SerialLink
  dispatch case **and** the advertised bit, so a build cannot advertise the
  capability without the handler.
- Stock ZiGate and any build without the command leave the bit clear and answer
  the request below with the generic `0x8000` status `2` ("unhandled"), which
  the host maps to `zigbee.ErrUnsupported`.
- The host must negotiate `0x0D0F` first and must not issue this command unless
  the bit is advertised.

## Command

| Name                          | ID       |
|-------------------------------|----------|
| `E_SL_MSG_GP_COMMISSION_REQ`  | `0x0D17` |
| `E_SL_MSG_GP_COMMISSION_RSP`  | `0x8D17` |

### Request `0x0D17` — exactly 7 bytes, no optional fields

| Offset | Size | Field             | Notes                                           |
|--------|------|-------------------|-------------------------------------------------|
| 0      | 1    | `version`         | MUST be `1` (`DIAG_REQ_VERSION`)                |
| 1      | 4    | `transaction_id`  | big-endian; host-chosen tag, any 32-bit value   |
| 5      | 1    | `action`          | `0` = disable, `1` = enable                     |
| 6      | 1    | `timeout_seconds` | `0` for disable; `1..255` for enable            |

Validation is strict. A wrong length, a wrong version, an `action` outside
`{0,1}`, a non-zero timeout with `action = 0`, or a zero timeout with
`action = 1` is rejected with the stock `0x8000` frame carrying
`E_SL_MSG_STATUS_INCORRECT_PARAMETERS`, and **no** `0x8D17` is emitted. The
`transaction_id` is deliberately *not* validated: it is opaque to the firmware,
and constraining it would make the host's counter a shared protocol concern
for no benefit.

### Response `0x8D17` — exactly 9 bytes

| Offset | Size | Field               | Notes                                          |
|--------|------|---------------------|------------------------------------------------|
| 0      | 1    | `version`           | `1` (`DIAG_RSP_VERSION`)                        |
| 1      | 4    | `transaction_id`    | big-endian; verbatim echo of the request's id   |
| 5      | 1    | `status`            | `0` = OK, `1` = Green Power stack error; `0` iff `gp_status == 0` |
| 6      | 1    | `effective_mode`    | `0` = operating/closed, `1` = commissioning open|
| 7      | 1    | `effective_timeout` | seconds programmed; `0` when closed             |
| 8      | 1    | `gp_status`         | underlying `teZCL_Status` (`0` = success)       |

A well-formed request always produces the stock `0x8000` success frame first,
then this response; the operation outcome lives in `status` / `gp_status`, not
in the outer status byte. On any failure the firmware reports the honest
post-operation state: `effective_mode = 0` and `effective_timeout = 0`, with
the `transaction_id` still echoed.

As with every custom response, `vSL_WriteMessage()` appends one **LQI byte**
past these 9 bytes and includes it in the declared serial length. The host
strips it (`stripZiGateResponseLQI`) before validating. That byte is not an
over-the-air link measurement for a local command and is not exposed.

## Host behaviour

`adapter/zigate/greenpower.go`:

- gates on the negotiated capability bit and returns `zigbee.ErrUnsupported`
  through the shared `requireRuntimeCapability` gate otherwise, sending nothing;
- rejects `enable` with a zero timeout locally rather than sending a request the
  firmware is required to refuse;
- normalises `disable` to `timeout = 0`;
- allocates a fresh 32-bit `transaction_id` per request **after** acquiring the
  serialized request lock (`sendCorrelatedCommandWait`), so the id always
  belongs to the transaction that is actually in flight and cannot wrap onto a
  still-outstanding one;
- registers the `0x8D17` waiter **before** the request is written
  (`sendCommandWaitDurationLocked`), so a fast response cannot be lost;
- matches a response *only* on exact length, version and the full 32-bit
  `transaction_id`. Anything else — including a perfectly valid response
  belonging to an earlier transaction — is discarded and the transaction keeps
  waiting until its own response arrives or the response timeout expires;
- then strictly validates the semantic fields (`effective_mode` domain, the
  `status`/`gp_status` consistency invariant, and the reported mode and window
  against the request);
- surfaces the firmware operation status and the underlying ZCL/GP status in
  the returned error.

## Out of scope

Green Power **shared-key programming** remains unsupported and unadvertised.
Neither the stock serial protocol nor this ABI exposes any GP key-material
command, so `GPSetSharedKey` returns `zigbee.ErrUnsupported` even when the
commissioning capability is negotiated. No key material, TCLK state or PDM
content is exported by this command.
