# Boot reset diagnostic UART ABI

Firmware build revision 12 introduced a read-only, capability-gated boot reset
snapshot without changing diagnostic protocol 1.2 or any existing command
layout. Build revision 14 implemented the previously reserved exception-reason
byte without changing the response length. Revision 15 adds a separate
fixed-length exception-context response; the original `0x8D2B` remains exactly
six bytes. Revision 16 gives that command an independent capability bit without
changing either response layout. Revision 17 changes only APS key-index
handling and leaves both diagnostic commands unchanged.

## Negotiation

- Summary request/response: `0x0D2B` / `0x8D2B`
- Context request/response: `0x0D2C` / `0x8D2C`
- Summary capability bit: `1 << 18` (`0x0000000000040000`)
- Context capability bit: `1 << 19` (`0x0000000000080000`)
- Wrapper-default capability bitmap: `0x00000000000CC60F`
- Wrapper-default build ID: `0x010DC53D`

Hosts must negotiate `0x0D0F` and require bit 18 before sending `0x0D2B`.
They must independently require bit 19 before sending `0x0D2C`. Revisions
12-14 shipped bit 18 with only the `0x0D2B` contract, so bit 18 must never be
used to infer reset-context support.

## Request

The application payload is empty. With the default XOR checksum, the complete
escaped ZiGate request frame is:

```
01 02 1D 2B 02 10 02 10 26 03
```

This is start `01`, type `0D2B`, length `0000`, checksum `26`, end `03`;
logical bytes below `0x10` are escaped as `02, byte^0x10`.

Any non-empty request receives only the stock `0x8000` status with
`INCORRECT_PARAMETERS` (`1`).

## Successful response

The firmware first emits the normal `0x8000` success status for request
`0x0D2B`, then emits `0x8D2B`. The `0x8D2B` application payload is exactly six
bytes:

```
offset  size  field
0       1     response_version = 01
1       1     status = 00
2       1     reset_flags
3       2     captured SYSCTRL status, unsigned big-endian
5       1     retained reset reason
```

`vSL_WriteMessage()` then streams one trailing LQI byte (`00` here), so the
logical serial length in the `0x8D2B` frame is `0007`. The unescaped logical
frame body is therefore:

```
8D 2B 00 07 CRC  01 00 FLAGS SYS_HI SYS_LO FF 00
```

`CRC` is the XOR of type, length, the six application bytes, and trailing LQI.
Normal ZiGate escaping applies to every byte from type through LQI; start/end
remain unescaped.

Reset flags:

- bit 0 (`0x01`): `bAHI_WatchdogResetEvent()` was true;
- bit 1 (`0x02`): `bAHI_BrownOutEventResetStatus()` was true;
- bits 2..7: zero.

Reset reasons:

- `0x01`: bus error;
- `0x02`: alignment error;
- `0x03`: illegal instruction;
- `0x04`: stack overflow;
- `0x05`: unclaimed exception/interrupt;
- `0x10`: host reset command;
- `0x11`: erase-PDM command;
- `0x12`: factory reset;
- `0x13`: development raw-PDM reset;
- `0xFF`: unavailable, including a cold boot or invalid retained marker.

The 16-bit status is captured with `u16AHI_PowerStatus()` at the first
statement of `vAppMain()`. It is the low 16 bits of the JN5169 SYSCTRL status
register; SDK register definitions identify watchdog-reset at raw bit 7 and
brownout-reset at raw bit 8. The separate flags use the public SDK APIs rather
than requiring hosts to infer those bits.

Revision 14 stores a small magic/inverse/checksum-guarded marker in a linker
`NOLOAD` SRAM section immediately before a deliberate software reset. The C
runtime neither copies nor clears that section. The marker is consumed and
cleared at the next `vAppMain()` entry, so random SRAM after a cold power-on
cannot be accepted without passing all guards. The snapshot performs no PDM
write, contains no key or credential data, and remains constant until the next
physical/firmware boot.

## Revision 15 exception context

The `0x0D2C` request has an empty payload. With the default XOR checksum, its
complete escaped ZiGate request frame is:

```
01 02 1D 2C 02 10 02 10 21 03
```

The firmware first emits normal `0x8000` success status, then `0x8D2C`. The
application payload is exactly 22 bytes:

```
offset  size  field
0       1     response_version = 01
1       1     status = 00
2       1     retained reset reason
3       1     reset flags
4       2     captured SYSCTRL status, big-endian
6       4     EPCR, big-endian
10      4     EEAR, big-endian
14      4     exception-handler SP, big-endian
18      4     interrupted LR/r9, big-endian
```

`vSL_WriteMessage()` appends LQI, so the serial frame length is `0x0017`.
Non-exception reset paths report `FFFFFFFF` for EPCR, EEAR, SP and LR. The
guard checksum covers the reason and all four register values.

The bus-error handler reads EPCR, EEAR, r1 and r9 before its first function
call. The compiler-generated handler prologue reserves eight bytes before r1
is sampled, so the reported SP is the C-handler SP, not the exact pre-exception
r1. EPCR is the authoritative faulting instruction address; EEAR is the
effective address associated with the exception. The exception handler no
longer walks and dereferences the stack before reset, because that diagnostic
loop could recursively bus-fault and obscure the original context.
