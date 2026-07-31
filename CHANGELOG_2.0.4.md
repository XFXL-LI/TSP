# Firmware 2.0.4

Date: 2026-07-31

## Evidence

- Hardware logs showed complete minute collection and SD storage while live
  delivery was skipped for several minutes after a CSQ command returned `99`.
- The LED receiver queue reached `waiting=5 spaces=0`. Five processed packets
  remained retained, matching the observed stepwise heap reduction.
- Pending packet files were structurally intact and had no leading zero bytes.
- Hour files and HJ212 hour packets were generated and acknowledged normally.

## Changes

- An invalid CSQ command result no longer overwrites the last valid CSQ.
- Added `CSQ_INVALID_KEEP` diagnostics for invalid CSQ results.
- Live HJ212 upload remains eligible after a transient CSQ command failure;
  the actual packet ACK determines whether the packet is saved for retry.
- The RS485 LED display now processes only real-time packets. Minute, hour,
  and day packets are released immediately instead of consuming a full display
  cycle and filling the LED queue.
- Added `LED_SKIP_NONREAL` diagnostics.
- Kept the field-test HJ212 packet gap at 3000 ms.

## Compatibility

- LCD `get_data` behavior and polling interval are unchanged.
- The LED real-time display cycle duration is unchanged.
- Minute, hour, and day collection, calculation, boundary settlement, storage,
  packet content, and dispatch rules are unchanged.
- Pending packet format and SD write verification are unchanged.

## Validation required

- Confirm transient invalid CSQ results do not create consecutive live gaps.
- Confirm the LED queue no longer remains full.
- Confirm free heap and largest block stabilize during a multi-hour test.
- Confirm hour files and `type=2` uploads remain unchanged.
