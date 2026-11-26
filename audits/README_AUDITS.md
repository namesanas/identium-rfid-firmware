# Identium in-house boards — audit set

**3 November source update:** [Small fixes implementation record](IDM-AUD-004_Small_Fixes_2026-11-03.docx) ([Markdown](IDM-AUD-004_Small_Fixes_2026-11-03.md)) records eight localized fixes, three partial fixes and passing host/build checks in this working tree. The reports below describe earlier deliveries; hardware validation and broader release blockers remain open.

Audits of the in-house RFID boards (the INB1 controller board and, since November, the handheld BLE reader), newest first. Each is a standalone Markdown file written to be read by an engineer **or by Claude**: every finding carries the designator, pin, net name or coordinate it rests on, so it can be checked against the source files rather than taken on trust.

| Report | Date | Scope | Verdict |
|---|---|---|---|
| [IDM-AUD-005](BLE%20reader%20audit/IDM-AUD-005_BLE_Reader_PCB_Audit.md) | 2 Nov 2026 | **Separate product:** handheld WIFI/BLE UHF reader board "MUBR" V1.0 (31 Oct EasyEDA package) | **Not as drawn: fix, then order prototypes.** D4 stops the unit from ever switching off (critical, BOM fix). The TPS63802 layout blocks fab. Battery pack unspecified |
| [IDM-AUD-004](271026/IDM-AUD-004_Firmware_Rev3_ReAudit.md) | 2 Nov 2026 | INB1 firmware rev 3 (271026 drop) | **Lot 1 bench use only; not for pilot.** Signing private key leaked in the zip. Offline buffer about 65k records and never replayed. Bootloader has no rollback and a 112 KB ceiling |
| [231026 board check](231026/RFP/231026_Board_Check.md) | 23 Oct 2026 | INB1 13-Oct board (adds GD25Q32 flash + Wiegand) | Lot 1 prototype (10) go; pilot only after Lot 1 tests. RFP IDM-RFP-001 issued from this |
| [IDM-AUD-003](IDM-AUD-003_Fabrication_Release_Audit.md) | 11 Oct 2026 | The "final submit" fab package: Gerbers, drill, BOM, pick-and-place | **DO NOT ORDER** — 8 findings block fabrication |
| [IDM-AUD-002](IDM-AUD-002_Firmware_Rev2_ReAudit.md) | 6 Oct 2026 | Firmware revision 2 | No-go for prototype or pilot; still cannot build for the F411 |
| [IDM-AUD-001](IDM-AUD-001_Engineering_Audit.md) | 30 Sep 2026 | First full delivery: firmware, schematic, PCB, BOM, GUI, memo | Not ready; 185 findings |

**Current word (2 Nov 2026):** INB1 hardware is covered by the 231026 board check, which builds on IDM-AUD-003. INB1 firmware is covered by **IDM-AUD-004**, which re-checks all 130 IDM-AUD-002 findings: 6 fixed, 64 partly fixed, 53 not fixed, 7 regressed. **IDM-AUD-005** is a different board, the battery handheld reader, and is not part of INB1.

**Action that cannot wait (IDM-AUD-004 SEC-01):** `271026/STM32_Firmware.zip` contains `keys/fw_signing_key.pem`, the private key that every bootloader built from that tree trusts. Do not forward the zip. Identium must generate its own key and rebuild the bootloader before any unit leaves the building. **RFP knock-on:** firmware rev 3 accepts only the GigaDevice GD25Q32 flash, so the RFP's Winbond Option B needs a firmware change first.

Evidence folders: `271026/_fw/work/` (firmware: `LEAD_FACTS.md`, `fw_results.json` with every verifier vote) and `BLE reader audit/_ble/` (BLE board: `net.py`, `netlist.json`, `ble_results.json`, renders). Both reports were generated from those JSON files by `_reports/build_audit.js`.

Before 2 Nov, the guidance was: start with **IDM-AUD-003**. It re-checks the hardware findings from 001 and supersedes them where the board changed. **IDM-AUD-002** was the word on firmware rev 2. **IDM-AUD-001** is still the best source of detailed evidence for anything the later two mark "not fixed".

PDF and Word versions of each sit beside the Markdown for circulation.

---

## The short version

**The board got substantially better.** Of the findings carried forward, 8 are fixed outright and 26 are partly fixed. September's board had ~15 unrouted MCU pads, no copper pours, a mis-pinned buzzer transistor, a reversed electrolytic, resistors shorting the Ethernet pair to logic ground, and a CN1 pinout that would have destroyed the first module plugged in. All of that is gone. The board is now fully routed with no shorts or opens, the buck matches its reference design value for value, and BOM, pick-and-place and copper agree exactly.

**It still must not be fabricated**, for three reasons that no component swap or rework can address:

1. **No memory device, and no pins left to add one.** The ≥100k-tag offline buffer is why this programme exists. 33 of 36 GPIOs are committed; the only free pads are PC14/PC15 (VBAT domain) and PA1.
2. **Power and Ethernet layout faults.** Buck input hot loop ≈11.9 mm against a ~1 mm datasheet rule; no capacitor anywhere on the 5 V rail; 21–30 mm of unreferenced Ethernet MDI with the receive pair split by its own centre tap; a 613 mm² ground void under every field connector.
3. **No schematic, stackup, copper weight or assembly drawing** in a package named "final submit".

A respin is already required for the requirement set, so ordering now does not buy a shortcut — it buys the same respin later, plus the cost of the boards.

**The one question that settles the most:** is this a 9–24 V product or a 12 V product? The input front end is now consistently built for 12 V (SMAJ16A TVS, 16 V fuses, an EN divider that exceeds the buck's 6 V limit above ~14 V), while the requirement and the team's own memo both say 9–24 V. At 24 V the TVS diodes avalanche and are destroyed, after which the fuses stand off 24 V against a 16 V rating.

---

## Verifying any finding yourself

The fab package shipped no schematic, but `FlyingProbeTesting.json` inside the Gerber zip contains a **complete pin-to-net netlist** (1,380 pins, 219 nets). It was extracted and is the strongest evidence available — stronger than the September review had, when the PCB export omitted pours and net names.

Everything lives in [`101026/_a3/`](101026/_a3/):

| File | What it is |
|---|---|
| `net.py` | Netlist query tool (below) |
| `netlist.json` | `{pins: {"U7.2": {net,x,y,layer,typ}}, nets: {net: [pins]}}`, coordinates in mm |
| `netlist_readable.txt` | Every component, every pin, its net and the other pins on that net |
| `BOM.tsv`, `PNP.tsv` | BOM and pick-and-place as TSV |
| `gerber/` | Unpacked Gerber, drill and flying-probe files |
| `render_composite.png`, `render_top.png`, `render_bottom.png` | Renders generated from the Gerbers |
| `render_gerber.py` | The renderer, if you want to re-render a region |
| `fab_results.json` | Machine-readable form of IDM-AUD-003 |
| `aud001_cards.json`, `aud002_cards.json` | Machine-readable forms of the earlier two |

```bash
cd 101026/_a3
python3 net.py comp U15          # every pin of U15 with its net and neighbours
python3 net.py pin U7.2          # one pin: its net and everything else on it
python3 net.py net '$1N593'      # everything on a net  (this one is the 5 V rail)
python3 net.py find 10k          # BOM search
python3 net.py rails             # largest nets, i.e. the power rails
python3 net.py unconnected       # pads sitting on a single-pin net
```

A net named `$1Nxxx` is real. A net named `NET_n` with exactly one pin is an **unconnected pad**.

Three checks worth doing first, because the most consequential conclusions rest on them:

```bash
python3 net.py net '$1N593'      # 5 V rail: 15 components, not one capacitor
python3 net.py comp U15          # buck: EN divider R38/R63 exists; see distances below
python3 net.py comp CN3          # "4G_DATA" sits on the same 4 nets as CN8 "LAN"
```

Buck geometry, measured pad-centre to pad-centre from `netlist.json`:

| Path | Distance | Reference |
|---|---:|---|
| C58+ → U15.2 (VIN) | 4.89 mm | MPS: input cap within ~1 mm |
| C58− → U15.4 (GND) | 7.03 mm | — |
| **input hot loop** | **≈11.9 mm** | **~1 mm** |
| U15.3 (SW) → L3.2 | 6.93 mm | keep minimal |
| U15.8 (FB) → R37.2 | 7.68 mm | keep at the pin |
| U15.6 (EN) → R38.2 | 35.29 mm | keep local |

---

## Using these with Claude

Point a session at this folder:

```
Read README_AUDITS.md and IDM-AUD-003_Fabrication_Release_Audit.md, plus the
artifacts in 101026/_a3/. Verify finding F-PCB-01 against the netlist and the
MP2338 datasheet, and tell me whether you agree, disagree, or cannot tell from
the package.
```

Useful prompts:

- *"List every finding in IDM-AUD-003 that blocks fabrication, with the single sentence of evidence each rests on."*
- *"I am going to re-lay the power stage. Give me every finding that touches U15, C58, L3, R37/R39/R40 or R38/R63, and the exact change each one asks for."*
- *"Cross-check the IDM-AUD-003 requirement table against the Identium host protocol PDFs in `../sdk/` and tell me which protocol gaps are hardware-caused and which are firmware-only."*
- *"Treat me as the design team. Argue against finding X using the artifacts."* — this is worth doing; see below.

### Please argue with it

Four verdicts in this set were produced by reviewers and then **overturned by the lead auditor** after direct measurement:

- **PCB-09** — returned as "buck layout fixed". Refuted: the hot loop is 11.9 mm.
- **HW-09** — returned as "4 outputs and Wiegand fixed". Refuted: there are 3 relay outputs and no Wiegand anywhere in the netlist or silkscreen.
- **"USART1 double-booked"** — carried as HIGH by majority vote. Refuted: PB6/PB7 to the ESP32 carry 4.7 kΩ pull-ups to 3V3 (R28/R30), so that link is I²C; the RFID UART is separately on PA9/PA10. Replaced with a narrower, real finding about I²C SDA sharing the IO8 strapping pin.
- **"Power-path status outputs unrouted"** — carried as blocking. Re-classified high but not blocking: both pads are netless and can be bodged.

Each report lists its refuted candidates in an appendix so nobody re-raises them. Findings marked *single-reviewer* (low and info) had one reviewer and no adversarial check — treat those as leads, not conclusions.

---

*Identium Tech Solutions · Confidential · Creating identities for all*
