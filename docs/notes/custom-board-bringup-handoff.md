# Custom board bring-up handoff

Updated November 21, 2026. The custom STM32F411 reader runs on USB-C power and responds over USB, Wi-Fi, and wired Ethernet. The reader's wired address is now **`192.168.1.35:5000`**. TAGlink can browse and export the onboard tag log. The 12 V reset fault and live RFID tag reads remain open.

## Resume at the bench

1. For normal operation, power the board over USB-C and connect Ethernet to the router. In **TAGlink Common → IF9D → Settings → Network & HTTP → Discover readers**, select `192.168.1.35`. Choose **Ethernet** in the top dropdown and click **Connect**. USB COM13 also works.
2. When an oscilloscope is available, trigger on the falling edge of NRST at CN7 pin 4 while adding 12 V to the USB-C-powered board. Use CN7 pin 3 as ground. Compare the reset edge against CN7 pin 2 (3.3 V), TP11 (mux output), TP12 (buck output), and TP9 (12 V input). Check the MCU analog and core supply nodes at C12 and C76 if accessible.
3. Repeat live RFID inventory with a known-good antenna and tag. Remaining functional checks include actual switching of the four digital inputs, relay engagement and release, audible buzzer operation, and a signed field update on the F411. The tag log's full-clear operation has been tested in simulation. Use a reader with expendable records for a physical clear test.

## STM32 flash and power

- The Discovery's onboard ST-LINK programmed the custom STM32F411CEU6 over SWD. CubeProgrammer identified `0x431` with 512 KB of flash. The successful connection used `port=SWD mode=NORMAL reset=SWrst freq=480`.
- CubeProgrammer verified both `STM32_Firmware/build/boot-f411/bootloader.elf` at `0x08000000` and `STM32_Firmware/build/f411-release/rfid_controller_f411.elf` at `0x08004000`. After reset, the vector table register pointed to `0x08004000` and the application answered on **COM13**.
- SWD worked with the custom board powered through its **USB-C port**. On 12 V alone, ST-LINK recognized the probe and returned `Unable to get core ID` for the target.

### 12 V power fault

| Test | Observed result |
|---|---|
| 12 V only | LED3 stayed off before and after KEY1 reset. CN7 pin 2 measured **3.27 V** to pin 3. NRST (pin 4) measured **0 V** with the reset button released and no SWD reset lead attached. |
| USB-C only | NRST measured **3.25 V** and LED3 was on. The application answered on USB and the network. |
| Add 12 V while running from USB-C | LED3 turned off as soon as 12 V was added. C1 at the U3 input measured **4.7 V** on USB-C alone and with both supplies connected after LED3 went out. |
| Alternate supply and load | The failure persisted with a second 12 V supply and with the RFID module disconnected at CN1. |

The low reset voltage on 12 V is confirmed. The cause and timing are still open. The schematic and Gerber review identified the U13 power-mux handoff and U15 buck input layout as areas to check against a scope capture. The measurements above were taken with a multimeter.

### SWD wiring that worked

The **32F429IDISCOVERY** connected to the PC through its **CN1 Mini-USB**. Windows showed **ST-Link Debug** and the probe's serial port as **COM3**. Both **CN4 jumper caps** were removed. The table uses the pin markings on each header.

| Discovery CN2 | Custom CN7 | Signal |
|---|---|---|
| Pin 1 | Pin 2 | 3.3 V target sense |
| Pin 2 | Pin 5 | SWCLK |
| Pin 3 | Pin 3 | GND |
| Pin 4 | Pin 6 | SWDIO |

## Bench checks

**16 checks through November 21.** Eleven passed, four failed on the bench, and the buzzer passed at the command level only. USB-C powered the board for the successful tests.

| Check | Last result |
|---|---|
| USB and MCU on COM13 | Six consecutive `GET_DIAG` replies, increasing uptime, `FAULTS=0`, `BOR=3` |
| Bootloader | `FW_STATUS` returned `BOOT=1` and `STATE=IDLE` |
| Onboard SPI flash | `TAGLOG_STATUS` returned `READY`. The latest tested log exposed 65,307 retained records. |
| TAGlink tag memory | First and latest pages and exact firmware sequence `40012` verified. |
| TAGlink full CSV export | Exported all **65,307** records in about **59 seconds**. Sequence order, uniqueness, and the contents of record `40012` were checked. |
| Digital inputs | `GET_GPI` returned `0` on all four inputs at rest |
| Onboard buzzer | `SET_BUZZER,300` returned `OK,SET_BUZZER`. Audible confirmation is pending. |
| RFID module commands | The latest bench session returned the real module version and current antenna and power settings. `GET_DIAG` reported `DEGRADED=NONE`. |
| Live RFID tag inventory | `SINGLE_POLL,1000` returned `NO_TAG` during the bench test. |
| Antenna VSWR | The module returned `0xAA4A` and zero samples. The antenna warning remains unresolved. |
| ESP32-C3 | Flashed and hash-verified. After a reboot, `GET_WIFI` on COM13 reported `LINK=ONLINE`. |
| Wi-Fi | Station mode on `Airtel_Identiumproduction`. `GET_DIAG` answered over TCP at `192.168.1.62:5000` during the bench test. The Wi-Fi lease can change. |
| Ethernet | Verified the wired MAC, TAGlink discovery, and reader commands on `192.168.1.35:5000`. |

The four failed checks were the two 12 V tests, live tag inventory, and VSWR. The current tag log was preserved during testing.

## Ethernet configuration and verification

- The reader's original Ethernet address was `192.168.150.50/24`. An on-link bench test confirmed its wired MAC and verified `GET_DIAG` and `RELEASE` on TCP port 5000 at that address.
- The laptop is on `192.168.1.11/24` over Wi-Fi. The requested `192.168.1.20` was already in use. The chosen `192.168.1.35` gave no ARP response before assignment.
- The new static settings were saved via `IPSET` on **COM13** and verified by `IPGET`. Use **IP `192.168.1.35`**, **mask `255.255.255.0`**, and **gateway `192.168.1.1`**. The wired MAC is **`02:1D:79:5F:BD:34`**. The configuration port is `8080`, and the TAGlink command port is `5000`.
- TAGlink's connection code discovered the reader at its new address and received `GET_NETWORK`, `GET_DIAG` (`FAULTS=0`, `DEGRADED=NONE`), and `OK,RELEASE` over a normal Ethernet TCP connection.
- [CN8 LAN connector pinout from the Gerber](../hardware/CN8-LAN-connector-pinout.png). Pin 1 is TD+, pin 2 is TD−, pin 3 is RD+, and pin 4 is RD−.

## TAGlink tag memory

- **IF9D → Tools → Tag memory** provides log status, paging, lookup by firmware sequence number, CSV export, and confirmed full-log clearing. Firmware supports clearing the entire log. The four memory workflow checks and 28 existing automated checks passed.
- Live memory testing used TAGlink source on COM13. The full log was exported and retained on the reader. Full-log clear was exercised in simulation.
- [TAGlink.exe](../../TAGlink_Desktop_common/dist/if9d-tag-memory/TAGlink.exe) is the packaged Windows build. [Memory workflow tests](../../TAGlink_Desktop_common/tools/check_tag_memory.py) are in the app source tree.

## ESP32-C3 programming record

The **3.3 V CP210x USB-to-UART adapter** appeared as **COM14**. At **CN11 (`WIFI_ESP`)**, pin 3 went to adapter GND, pin 4 (ESP TX) went to adapter RX, and pin 5 (ESP RX) went to adapter TX. The custom board was powered through USB-C with the adapter's VCC lead disconnected.

Fit **J2** with power off to pull GPIO9 low. Hold **KEY1** while applying power and throughout programming so the STM32 stays in reset. Without J2 fitted, the blank ESP32-C3 repeatedly reported `invalid header: 0xffffffff` over UART. The first write stopped after the connection dropped. The retry below detected an ESP32-C3 rev v0.4 with 4 MB of flash and verified the hash of the complete 857,584-byte image. Update the port if Windows assigns a different number.

```powershell
$espPython = 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
& $espPython -m esptool --chip esp32c3 -p COM14 -b 115200 --before no-reset --after no-reset `
  write-flash 0x0 .\ESP32_Firmware\build_esp32c3\wifi_coprocessor_esp32c3_merged.bin
```

After verification, the board was powered off, J2 was removed, and KEY1 was released. On restart the STM32 enumerated as COM13 and `GET_WIFI` confirmed that the coprocessor was online.

[Custom board schematic](../hardware/SCH_Schematic1_2026-10-13.pdf) · [F411 bring-up guide](f411-custom-board-port.md) · [ESP coprocessor guide](dev-guide-esp32-coprocessor.md)
