"""Generate the revised source-review report using only Python's standard library."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from xml.sax.saxutils import escape
from xml.etree import ElementTree as ET
import hashlib
import re
import subprocess
from collections import Counter

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'audits'
BASE = 'IDM-AUD-004_Current_Checkout_Review_2026-11-03'
original = OUT / 'IDM-AUD-004_Firmware_Rev3_ReAudit.docx'
original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
F = 'STM32_Firmware/'
items = []

def ref(path, needle):
    lines = (ROOT / path).read_text(encoding='utf-8', errors='replace').splitlines()
    for number, line in enumerate(lines, 1):
        if needle in line:
            return f'{path}:{number}'
    raise ValueError((path, needle))

def add(id, status, title, finding, evidence, action):
    items.append(dict(id=id, status=status, title=title, finding=finding,
                      evidence=evidence, action=action))

add('BUF-01', 'CONFIRMED', 'Automatic offline replay is absent',
    'The flash log is separate from the live upload queue. TagLog_Read is called by the manual TAGLOG_READ host handler, not by an automatic upload drain. The live queue drops old entries when full (256 entries or its byte limit) and expires entries after the configured age; the default is 8 seconds. These are defaults and bounds, not a guarantee of exactly 8 seconds of retention under every load.',
    [ref(F+'Firmware/Application/Host/host_interface.c', 'if(TagLog_Read(seq'), ref(F+'Firmware/Protocol/identium_protocol.c', 'static void Identium_Protocol_DiscardStaleTags'), ref(F+'Firmware/Config/product_config.h', 'PRODUCT_PROTOCOL_UPLOAD_CLR_R_BUF_TIME_S')],
    'Implement persistent FIFO replay with an acknowledged delivery cursor, reconnect recovery and explicit accounting for records overwritten before acknowledgement. Test disconnect/reconnect, reset during replay and duplicate handling.')
add('BUF-02', 'CONFIRMED', 'Tag-log capacity remains below 100,000 records',
    'The lower 2 MB of a 4 MB chip holds 32-byte records. The advertised guaranteed capacity is (512 - 2) x 128 = 65,280 records, with additional retention depending on head position. The upper 2 MB remains reserved for update staging. The original 65,376-record test result is historical; it was not reproduced in this review. The 100k target is taken from the audit, not independently revalidated against the original requirements package.',
    [ref(F+'Firmware/Services/storage/tag_log.c', '#define TAG_LOG_FLASH_BYTES'), ref(F+'Firmware/Common/firmware_image.h', '#define FW_STAGING_ADDRESS'), ref(F+'hostcheck/tag_log_host_test.c', 'CHECK(s.capacity == 65280U)')],
    'Repartition storage and revise sequence-to-slot mapping safely, or qualify a larger supported flash part. Add a requirement-level capacity check and migration/power-loss tests.')
add('SEC-02', 'CONFIRMED', 'Network host control lacks authentication',
    'The host TCP server listens on the default port 5000 and forwards traffic into the host command transport. Firmware update and TAGLOG_CLEAR handlers do not require an authenticated session. Signed-image verification is present, but it is not authorization for network control. Installing arbitrary code would additionally require an accepted signing key or another bypass; unauthenticated access alone does not establish that capability.',
    [ref(F+'Firmware/Services/ethernet/host_tcp_server.c', 'W5500_Socket3_Listen'), ref(F+'Firmware/Application/Host/host_interface.c', 'else if(strncmp(command, "FW_BEGIN,'), ref(F+'Firmware/Application/Host/host_interface.c', 'else if(strcmp(hostRxBuffer, "TAGLOG_CLEAR")')],
    'Define authenticated administrative access and gate destructive/control operations. Restrict exposure during bench work. Retain signature verification independently of authentication.')
add('SEC-03', 'CONFIRMED', 'No trial boot or runtime rollback',
    'The bootloader retries installation and marks a successfully copied image INSTALLED. There is no application-confirmed trial state or retained known-good application fallback in the inspected boot flow. A signed image that installs correctly but fails at runtime can leave the unit without a usable remote update path. Installation retry is not runtime rollback.',
    [ref(F+'Bootloader/boot_main.c', 'static uint8_t Boot_CheckStaging'), ref(F+'Bootloader/boot_main.c', 'BootStaging_SetState(FW_IMAGE_STATE_INSTALLED)')],
    'Design trial boot, confirmation and recovery before deployment. Validate with signed images that crash before host services start and with power loss during install.')
add('PRO-02', 'CONFIRMED', 'HTTP errors can discard uploads',
    'HttpUpload_FinishResponse changes the state to IDLE before checking whether a response is 2xx. The 2xx check controls command-body delivery, not successful acknowledgement of the uploaded batch. The failure path also gives up after HTTP_MAX_ATTEMPTS. Automatic flash replay is absent, so failed uploads have no demonstrated automatic recovery.',
    [ref(F+'Firmware/Services/ethernet/http_upload.c', 'static void HttpUpload_FinishResponse'), ref(F+'Firmware/Services/ethernet/http_upload.c', 'if(g_attempts >= HTTP_MAX_ATTEMPTS)')],
    'Keep pending data until an accepted server acknowledgement. Test redirects, 4xx, 5xx, disconnects and timeouts; use a defined retry/backoff policy and durable queue.')
add('REQ-02', 'CORRECTED / PARTLY OPEN', 'F411 slot is 112 KB; bootloader is not literally immutable',
    'FW_APP_MAX_SIZE remains 112 KB for F411. No supported bootloader update protocol was identified. However, "immutable" is too strong: the source does not establish a physically immutable bootloader. The original report itself notes that application code could rewrite an unprotected bootloader. Also, the remaining 384 KB is reserved for reader settings and two configuration sectors; describing it simply as unused omits those allocations. Historical application/RAM usage percentages have not been remeasured.',
    [ref(F+'Firmware/Common/firmware_image.h', '#define FW_APP_MAX_SIZE'), ref(F+'Firmware/Services/configuration/configuration_storage.c', '#define CONFIG_STORAGE_SECTOR_A'), ref(F+'Firmware/Services/configuration/reader_settings.c', 'READER_SETTINGS_SECTOR')],
    'Plan application growth, settings allocation, recovery and key rotation together. Do not treat ad-hoc bootloader self-rewriting as a supported safe update mechanism.')
add('SEC-05', 'PARTLY FIXED', 'Failed-install fall-through fixed; ordinary-boot verification still limited',
    'Boot_Install hashes staged first-block data plus programmed application bytes before writing the initial vector block. Boot_CheckStaging returns false after failed installation, and main calls Boot_StartApplication only when that check permits it. The audit claim that a failed/hash-mismatched install falls through is superseded. On an ordinary boot with no pending image, Boot_StartApplication still checks vector bounds rather than re-verifying the full installed application signature/hash.',
    [ref(F+'Bootloader/boot_main.c', 'Sha256_Init(&sha)'), ref(F+'Bootloader/boot_main.c', 'if(Boot_CheckStaging() != 0U)'), ref(F+'Bootloader/boot_main.c', 'static void Boot_StartApplication')],
    'Close the failed-install fall-through subclaim at source level. Keep the ordinary-boot integrity question open and validate interrupted-install recovery on F411 hardware.')
add('BUF-05', 'FIXED IN SOURCE', 'TAGLOG_CLEAR no longer erases the staging partition',
    'The clearing state now calls GD25Q_StartBlockErase for blocks below TAG_LOG_BLOCK_COUNT, which is derived from the lower-half log size. It no longer performs a chip erase. The specific staging-destruction finding is closed at source level. This does not close separate findings about sequence handling, reset recovery or flash contention.',
    [ref(F+'Firmware/Services/storage/tag_log.c', 'if(g_tagLog.state == TAG_LOG_STATE_CLEARING)'), ref(F+'Firmware/Services/storage/tag_log.c', 'GD25Q_StartBlockErase'), ref(F+'hostcheck/tag_log_host_test.c', 'The upper 2 MB is the firmware update staging area')],
    'Retain the staging-preservation regression test and exercise clear/update/reset interactions on hardware; tests were inspected, not executed here.')
add('NET-01', 'CORRECTED / PARTLY OPEN', 'ESP32-C3 source and an enabled F411 link are present',
    'ESP32_Firmware is an ESP-IDF project with Wi-Fi, sockets and I2C link code. The C3 branch assigns SCL to GPIO6 and SDA to GPIO10. F411 board_config.h enables the ESP link and assigns STM32 PB6/PB7, with PB9 for EN. The esp32_link.h comment saying GPIOs collide is stale relative to these definitions. Therefore "no ESP32 firmware, so Wi-Fi cannot work" is not valid for this checkout. End-to-end Wi-Fi operation on the actual board remains unverified here; the BLE portion is not revalidated.',
    [ref('ESP32_Firmware/main/main.c', 'void app_main'), ref('ESP32_Firmware/main/i2c_link.c', '#if defined(CONFIG_IDF_TARGET_ESP32C3)'), ref(F+'Firmware/BSP/f411_custom/board_config.h', '#define BOARD_ESP_LINK_ENABLED'), ref(F+'Firmware/Devices/esp32/esp32_link.h', 'The hardware is optional')],
    'Correct the stale comment, build the C3 target and test association, host traffic, reconnect and reboot on F411 hardware. Assess BLE separately against requirements.')
add('SEC-01', 'UNRESOLVED EVIDENCE', 'Historical signing-key exposure is not cleared',
    'The original audit reports that its delivery ZIP contained a private key matching the bootloader public key. That ZIP and its verification artifacts are not available in this checkout. A local keys/fw_signing_key.pem exists and is not tracked by Git; the Makefile defaults to that path. These observations do not prove whether it is the leaked key or a replacement. No private-key contents were copied into this report, and no key comparison or rotation was performed.',
    [ref(F+'Makefile', 'SIGN_KEY ?='), 'Local file existence and git ls-files checks; original audit SEC-01 (historical evidence only).'],
    'Keep this release gate open until key provenance/rotation and the installed bootloader trust root are established. Git exclusion alone does not undo a previous leak.')
add('BLD-03', 'CORRECTED / PARTLY OPEN', 'Repository context exists; reproducibility is unproven',
    'This checkout includes Git history, README build instructions, developer guides and design 0020. The original claim that these are absent applies to the audited ZIP, not this repository. The pre-existing tracked modification is a blank line in main.c. No clean rebuild, toolchain lock verification or comparison against a released binary was performed, so reproducibility remains unproven.',
    ['Git HEAD '+commit, 'README.md', 'docs/notes/dev-guide-stm32-firmware.md', 'docs/design/0020-bootloader-and-signed-update.md'],
    'Use a documented toolchain and clean revision for release artifacts, retain source-to-binary provenance and exclude signing secrets from delivery packages.')
add('BLD-05', 'CORRECTED / PARTLY OPEN', 'Tests exist, but requirement coverage remains incomplete',
    'The Makefile contains host checks for protocol, tag log, image verification and update receiving. The tag-log test explicitly expects 65,280 capacity, which does not assert the audit\'s 100k requirement. The open-items note records F429 successful-update and bad-signature hardware checks, while F411 update and tag-log checks remain listed as untested. These are recorded historical results, not newly witnessed test runs. A blanket "no on-target evidence" statement is too broad.',
    [ref(F+'Makefile', 'hostcheck:'), ref(F+'hostcheck/tag_log_host_test.c', 'CHECK(s.capacity == 65280U)'), 'docs/notes/open-items.md: Verified on hardware / Not yet tested on hardware'],
    'Add acceptance tests for capacity, acknowledged replay, interrupted settings writes and bad runtime images. Execute F411 hardware tests before release.')
add('ROB-02', 'CORRECTED / PARTLY OPEN', 'Reader settings append records; erasure occurs at rollover',
    'ReaderSettings_Save appends to erased slots. It erases the single reader-settings sector only when the slot area is full or the next slot is not erased. Therefore "every save erases the sector" is incorrect for this code. Power loss during rollover erase can still destroy all records in that sector. Separately, configuration_storage.c uses two other sectors and has the slot-selection defect described in CUR-01. The open-items note claiming reader-settings rollover is never erased is also stale.',
    [ref(F+'Firmware/Services/configuration/reader_settings.c', 'uint8_t ReaderSettings_Save'), ref(F+'Firmware/Services/configuration/reader_settings.c', '(g_nextSlot >= READER_SETTINGS_SLOT_COUNT)'), ref(F+'Firmware/Services/configuration/configuration_storage.c', '#define CONFIG_STORAGE_ADDRESS_A')],
    'Keep rollover power-loss resilience open. Distinguish the append-only reader-settings store from the separate A/B configuration store; validate both with fault injection.')
add('CUR-01', 'CONFIRMED', 'A/B configuration selection overwrites the newest slot',
    'Configuration_Storage_Save checks both V2 records, but whenever A is valid it selects B and sets the new sequence to A.sequence + 1, without comparing the two V2 sequences. Starting with A sequence 1, the next save writes B sequence 2; later saves keep erasing B and writing sequence 2 while A stays at 1. A power cut while rewriting B can lose the most recent saved settings and fall back to stale A. This is not a claim that all settings are inevitably lost.',
    [ref(F+'Firmware/Services/configuration/configuration_storage.c', 'else if(validA2 != 0U)')],
    'Compare valid V2 sequence numbers, write the older/invalid slot and advance from the newest sequence. Test repeated saves, sequence wrap and power loss at erase/program/verification boundaries.')

original_md = (OUT/'IDM-AUD-004_Firmware_Rev3_ReAudit.md').read_text(encoding='utf-8')
detail = original_md.split('## Findings in detail',1)[1].split('## Status of Every Rev 2 Finding',1)[0]
register = re.findall(r'^#### ([A-Z]+-\d+) — (.+)$', detail, re.M)
assert len(register) == 79 and len({i for i,_ in register}) == 79
by_id = {i['id']:i for i in items}
lines = []
def p(s=''): lines.append(s)
p('# IDM-AUD-004 — Current Checkout Review')
p('Updated findings • 3 November 2026 • INB1 firmware • Internal use')
p('This is a revised, limited source-review edition of the 2 November audit. It replaces the wording of the findings explicitly reviewed below for this checkout. The original Word report is retained unchanged as the historical delivery audit.')
p('## Decision and scope')
p('No pilot-release approval is supported by this review. Automatic offline delivery, capacity, authenticated control and runtime update recovery remain open. The historical signing-key exposure must also be resolved. Internal bench work is the appropriate scope pending fixes and validation.')
p('Baseline: Git '+commit+'. The working tree already contained one blank-line modification in STM32_Firmware/Src/main.c; it was preserved. No firmware was modified by this document update.')
p('Method: static inspection of local source and audit documents. No firmware rebuild, host-test execution, hardware test, original-ZIP inspection or electrical review was performed. Existing test outcomes are attributed to their documents. This is not a re-audit of all 79 new findings or all 130 carried-forward findings.')
p('Evidence boundary: only audit 004 in Markdown/Word and README_AUDITS.md are present locally. The audit index points to other reports and evidence directories that are absent. Original requirements and hardware conclusions are not independently certified here.')
p('## Corrections that matter')
for text in [
    'Confirmed: no automatic log replay; roughly 65k record capacity; unauthenticated network control; lossy HTTP error handling; no runtime rollback; defective A/B configuration selection.',
    'Fixed in source: TAGLOG_CLEAR is restricted to the log partition. Failed-install bootloader fall-through is fixed, but that does not establish full verification on every boot.',
    'Incorrect for this checkout: ESP32 firmware absent, F411 ESP link disabled by pin conflicts, Git/design documents absent, and reader-settings sector erased on every save.',
    'Unresolved: whether the local signing key is the previously exposed key, production release provenance, unreviewed audit claims and F411 hardware behavior.'
]: p('- '+text)
p('The original severity totals, scorecard and verification-vote counts describe the old delivery audit. They are not carried forward as measurements of this checkout. Unreviewed findings are neither accepted nor dismissed.')
p('## Reviewed findings')
for i in items:
    p('### '+i['id']+' — '+i['title'])
    p('Status: '+i['status'])
    p(i['finding'])
    p('Source evidence: '+'; '.join(i['evidence']))
    p('Required action / closure evidence: '+i['action'])
p('## Work order for closure')
for text in [
    'Resolve signing-key provenance and establish the intended bootloader trust root; define authenticated administrative access.',
    'Implement durable acknowledged replay and HTTP error retention, then meet the capacity requirement with safe sequence mapping and storage migration.',
    'Fix A/B configuration selection and make reader-settings rollover survive power interruption.',
    'Design trial boot/recovery alongside application sizing, configuration allocation and key rotation.',
    'Build and test both targets; on F411 validate Wi-Fi, tag logging, staging preservation, interrupted updates, repeated settings saves and reconnect replay.',
    'Recheck the remaining audit register against code, original requirements and hardware evidence before issuing a new release verdict.'
]: p('- '+text)
p('## Coverage register: 79 original revision-3 findings')
p('Each entry below identifies an original audit claim. Titles of entries marked NOT REVALIDATED are historical claim labels, not current conclusions. Detailed dispositions above govern reviewed entries. The 130 carried-forward revision-2 entries are not individually revalidated in this edition; consult the preserved original for their historical status.')
for id,title in register:
    if id in by_id:
        i=by_id[id]
        p('- '+id+' | '+i['status']+' | '+i['title'])
    else:
        p('- '+id+' | NOT REVALIDATED | Original claim: '+title)
p('CUR-01 is a separately recorded current-code defect and is not added to the original count of 79. Review coverage: '+str(len(set(by_id)&{id for id,_ in register}))+' original findings dispositioned; '+str(len(register)-len(set(by_id)&{id for id,_ in register}))+' not revalidated; one additional current-code finding.')
p('## Document provenance')
p('Original retained: '+original.name)
p('Original DOCX SHA-256: '+original_hash)
p('Companion Markdown has the same updated report text. Source paths are relative to the repository root; line references describe the current checkout and can change with later edits. No secret key material is included.')
markdown='\n\n'.join(lines)+'\n'
(OUT/(BASE+'.md')).write_text(markdown,encoding='utf-8')

# Minimal standalone Word package: standard OOXML, no external dependencies.
W='http://schemas.openxmlformats.org/wordprocessingml/2006/main'
def para(text, style='Normal'):
    return '<w:p><w:pPr><w:pStyle w:val="'+style+'"/></w:pPr><w:r><w:t xml:space="preserve">'+escape(text)+'</w:t></w:r></w:p>'
body=[]
for line in lines:
    if line.startswith('### '): body.append(para(line[4:],'Heading2'))
    elif line.startswith('## '): body.append(para(line[3:],'Heading1'))
    elif line.startswith('# '): body.append(para(line[2:],'Title'))
    elif line.startswith('- '): body.append(para('• '+line[2:],'ListParagraph'))
    elif line.startswith('Source evidence:'): body.append(para(line,'Evidence'))
    else: body.append(para(line))
document='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document xmlns:w="'+W+'" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body>'+''.join(body)+'<w:sectPr><w:footerReference w:type="default" r:id="rId2"/><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1000" w:right="1000" w:bottom="1000" w:left="1000" w:footer="450"/></w:sectPr></w:body></w:document>'
styles='<?xml version="1.0" encoding="UTF-8"?><w:styles xmlns:w="'+W+'"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="22"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="120"/><w:widowControl/></w:pPr></w:pPrDefault></w:docDefaults>'
for name,size,color in [('Normal',22,'202B38'),('Title',38,'16324F'),('Heading1',29,'16324F'),('Heading2',24,'245A7A'),('Evidence',18,'526170'),('ListParagraph',21,'202B38')]:
    extra='<w:keepNext/><w:keepLines/><w:spacing w:before="220" w:after="120"/>' if name in ('Title','Heading1','Heading2') else ''
    if name.startswith('Heading'): extra+='<w:outlineLvl w:val="'+('0' if name=='Heading1' else '1')+'"/>'
    if name=='ListParagraph': extra+='<w:ind w:left="220" w:hanging="180"/>'
    styles+='<w:style w:type="paragraph" w:styleId="'+name+'"><w:name w:val="'+name+'"/><w:pPr>'+extra+'</w:pPr><w:rPr><w:sz w:val="'+str(size)+'"/><w:color w:val="'+color+'"/>'+('<w:b/>' if name in ('Title','Heading1','Heading2') else '')+'</w:rPr></w:style>'
styles+='</w:styles>'
types='<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/><Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/></Types>'
rels='<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>'
docrels='<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/></Relationships>'
footer='<?xml version="1.0"?><w:ftr xmlns:w="'+W+'"><w:p><w:pPr><w:jc w:val="right"/></w:pPr><w:r><w:rPr><w:sz w:val="18"/></w:rPr><w:t>IDM-AUD-004 | Source review | 03 Nov 2026 | Page </w:t></w:r><w:fldSimple w:instr="PAGE"><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p></w:ftr>'
path=OUT/(BASE+'.docx')
with ZipFile(path,'w',ZIP_DEFLATED) as z:
    for name,data in {'[Content_Types].xml':types,'_rels/.rels':rels,'word/document.xml':document,'word/styles.xml':styles,'word/_rels/document.xml.rels':docrels,'word/footer1.xml':footer}.items(): z.writestr(name,data.encode('utf-8'))
with ZipFile(path) as z:
    assert z.testzip() is None
    for name in z.namelist(): ET.fromstring(z.read(name))
    root=ET.fromstring(z.read('word/document.xml'))
    actual=[''.join(p.itertext()) for p in root.findall('.//{'+W+'}body/{'+W+'}p')]
    expected=[re.sub(r'^#{1,3} ','',line) if not line.startswith('- ') else '• '+line[2:] for line in lines]
    assert actual==expected
assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
print(path)
print('Validated DOCX ZIP/XML and complete paragraph text; original preserved. Reviewed entries:',len(items))
