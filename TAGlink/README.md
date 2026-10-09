# TAGlink

Desktop application for the Identium RFID controller. It drives the reader through the firmware's text host commands over USB, RS232 / RS485 or Ethernet (TCP 5000), and runs VSWR scans through the module bridge (TCP 8080 or the same serial link).

## Workspaces

| Workspace | Purpose |
|---|---|
| Inventory | Start / stop continuous inventory, single and sync polls, live tag table and analytics |
| Tags | Read, write, EPC write, lock and kill |
| Settings | RF settings, work mode (passive / active), GPIO, network and HTTP upload |
| Diagnostics | VSWR (return loss) scan, module capabilities |
| Log | Command, response and raw traffic logs |

**Work mode.** Passive: the reader answers host commands only. Active: the reader runs its autonomous inventory and upload. While the reader is in active mode a *Return to passive* button is shown in the top bar; it stops the inventory and switches the reader to passive in one step. *Apply HTTP* only stores the upload settings for the next active session.

## Running from source

Requires Python 3.12 with PySide6 and pyserial.

```
pip install PySide6 pyserial
python main.py
```

## Building the Windows executable

```
pip install pyinstaller
pyinstaller main.spec
```

The single-file executable is written to `dist/TAGlink.exe`.

## Files

| File | Role |
|---|---|
| `main.py` | Application logic and the host command queue |
| `ui_builder.py`, `theme.py` | Layout and light / dark themes |
| `serial_manager.py`, `protocol.py` | Serial / TCP transport and reply parsing |
| `vswr_transport.py` | ModuleAPI-compatible VSWR request over the module bridge |
| `tools/smoke_screenshot.py` | Off-screen check: feeds sample replies and captures every workspace |
| `tools/screenshot.py` | Off-screen screenshots of each workspace |
