# Protocol Migration Step 5 Fix 2

## Issue observed on hardware

The TCP configuration-port `0x24` (ActiveModeJson) and `0x1E` (StaticModelJson) requests returned status 2, and the next connection could be reset.

## Root cause

`configuration_port.c` moved the large content buffer from a local array to a static array. The local variable remained a pointer:

```c
uint8_t *content = g_contentBuffer;
```

The code then used `sizeof(content)` as the buffer capacity. Since `content` is a pointer, `sizeof(content)` is only the host pointer size, not the 1416-byte buffer size. This caused both JSON builders to immediately fail their capacity check, and JSON padding had the same bug.

## Fix

Use `sizeof(g_contentBuffer)` for:
- `Configuration_Service_BuildActiveJson()`
- `Configuration_Service_BuildStaticJson()`
- `Configuration_Port_PadJson()`

No protocol behavior or GPIO behavior was changed.
