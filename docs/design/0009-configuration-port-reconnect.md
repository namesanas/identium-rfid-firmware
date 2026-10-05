# Protocol Migration Step 5 Fix 4

## Fixed issues

1. Configuration TCP 8080 reconnect race
   - Removed the fixed 20 ms forced-close timer after a response.
   - The reader now waits for the client to close the one-request connection, then handles `CLOSE_WAIT`, closes the W5500 socket, and immediately reopens the listening socket.
   - This prevents the next `state`, `active`, or `static` request from arriving while socket 1 is still being torn down.

2. Build warnings
   - Removed the unused `Configuration_Service_JsonStringSafe()` helper.
   - Removed the unused `g_escapedReaderName` buffer.
   - Removed the unused `uint16_t i` from `Configuration_Port_Task()`.

No protocol field, GPIO mapping, RFID behavior, or Socket 0 behavior was changed.
