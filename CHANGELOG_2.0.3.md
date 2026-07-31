# Firmware 2.0.3

Date: 2026-07-31

## Stability changes

- LCD `get_data` now reads only the requested sensor values into fixed-size
  buffers. It no longer creates a temporary EventBus response queue or clones
  the complete real-time `std::map` on every screen poll.
- Added allocation-failure protection to the legacy internal data-query route.
- Centralized all HJ212 serial arbitration in `DTUManager`.
- Live HJ212 upload has priority. CSQ returns a deferred state when an upload is
  active, waiting, or has just completed; the last known CSQ remains in use.
- Removed the transient 5 KiB pending-recovery task. The permanent maintenance
  task now restores one pending packet per five-minute maintenance cycle.
- A pending marker is retained when rebuilding is temporarily unavailable
  instead of being quarantined immediately.
- Serialized SD/FAT operations across storage, LCD history reads, and pending
  recovery.
- Pending HJ212 files are read back and byte-compared after writing. A failed
  verification is logged and the corrupt marker is removed.

## Compatibility

- HJ212 2017/2025 packet contents and the 1-second packet gap are unchanged.
- Minute, hour, and day statistic calculation and dispatch logic are unchanged.
- Screen and remote-control JSON response fields remain compatible.

## Diagnostic markers

- `GET_DATA_DIRECT`
- `DATA_QUERY_OOM`
- `CSQ_DEFER`
- `HJ_TX_DEFER`
- `SD_LOCK_BUSY`
- `PENDING_WRITE_VERIFY_FAIL`
- `RECOVERY_RETAIN`
- `RECOVERY_DONE`
