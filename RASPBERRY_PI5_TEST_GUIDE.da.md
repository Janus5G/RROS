# Uafhængig hardwarevalidering på Raspberry Pi 5

[English](RASPBERRY_PI5_TEST_GUIDE.md) · [Matrix](HARDWARE_COMPATIBILITY_MATRIX.md)

> **Aktuel sikkerhedsgate for fællesskabet: BLOCKED.** De fysiske procedurer
> nedenfor er historisk reference, ikke godkendte instruktioner. Udfør dem
> ikke, mens [sikkerhedsgaten](docs/community/SAFETY_GATE.json) har uafklarede
> kritiske krav. Brug [reviewvejledningen](docs/community/HARDWARE_GUIDE.md)
> og offlineværktøjerne.

## 1. Sikkerhed og omfang

**HARDWARE_NOT_VERIFIED.** Dette er ikke et produktionsinstallationsprogram
eller et verificeret Pi-operativsystem. Brug disponibelt flytbart medie,
sikkerhedskopier, egnet Pi 5-strømforsyning og køling. Frakobl værdifuldt lager
og alle aktuatorer/PLC'er. Formatering og rå skrivning sletter data.
Vejledningen giver ikke tilladelse til EEPROM-ændringer, indkøb, firmware-
flashing eller aktivering af hardware. Testeren/enhedsejeren skal selv godkende det.

**Pi5-probe-arkivet** var foreslået til indledende fysiske diagnoser, men
kørsel er nu blokeret, indtil sikkerhedskravene er afklaret.
Det komplette OS-udviklingsimage har stadig en QEMU-specifik UART-sti og mangler
Pi-firmwarekæden. Proben er et særskilt EFI-diagnoseprogram til firmwaretilstand,
FDT, UART, exceptions, hukommelse og timer. Den dokumenterer ikke fungerende
native periferi-drivere.

## 2. Dokumenter en fungerende baseline

Begynd med et eksisterende Raspberry Pi OS-kort. Gem:

```sh
cat /proc/cpuinfo
vcgencmd bootloader_version
vcgencmd version
uname -a
lspci -nn
lsusb -t
ip link
dmesg
```

Registrer præcis board revision, eventuel C1/D0-stepping, 4/8 GB RAM,
strømforsyning, køler, kabler, kort, USB-enheder, PCIe-HAT og periferiudstyr.
Angiv bootloader og medielayout, ikke blot "seneste firmware".
Hvis baseline har `dtc`, gem device tree:
`dtc -I fs -O dtb -o baseline.dtb /proc/device-tree`.
Baseline-succes er **ikke** RROS-succes.

## 3. Verificer release og firmware

Download kun fra det godkendte release-repository. Kontroller `SHA256SUMS` før
udpakning. Registrer tag, source commit og SHA-256 for både arkivet og
`card/EFI/BOOT/BOOTAA64.EFI`.

Kør disse read-only-softwarechecks fra det udpakkede probe-arkivs rod:

```powershell
python scripts\pi5_hw\build_probe_package.py --verify
python tests\pi5_hw_validation\selftest.py
python tests\pi5_hw_validation\evaluate_probe_log.py --mode qemu qemu_probe_serial.log
```

Den kopierede builder verificerer pakken selvstændigt; genbygning kræver det
komplette source-checkout. Installer pyserial 3.5 i et isoleret miljø til
self-testen. `evaluate_probe_log.py` klassificerer probe-logs; den medfølgende
`evaluate_serial_log.py` er kun til historiske G02/G12-logs.

Proben bundler bevidst **ikke** tredjepartsfirmware. Den fastlåste afhængighed er
[worproject/rpi5-uefi v0.3](https://github.com/worproject/rpi5-uefi/releases/tag/v0.3).
`RPi5_UEFI_Release_v0.3.zip` skal have denne SHA-256:

```text
2337df318845448ebcc8a6a587d15dccec44b54c665ece21250d2f6170732724
```

[Pins og individuelle filhashes](scripts/pi5_hw/boot_chain_pins.json) omfatter
`config.txt`, `bcm2712-rpi-5-b.dtb` og `RPI_EFI.fd`. Læs upstream-vilkår og
kompatibilitet før anvendelse. Firmwaren er arkiveret og upstream-testet på
tidlige BCM2712 C1-kort. D0 kan kræve nyere EEPROM og have skærmrisiko.
Upstreams UEFI-funktioner er ikke RROS-funktioner; RP1 Ethernet og EEPROM NVRAM
har kendte begrænsninger. Opdater ikke EEPROM blindt som fejlsøgningsgenvej.

## 4. Klargør kun disponibelt medie

1. Klargør selv et disponibelt FAT32-firmwarekort. Kontroller kortets identitet
   og sikkerhedskopier data **før** formatering.
2. Udpak den hash-verificerede firmware til FAT32-roden med originale navne.
   Kopier probens `card/EFI/BOOT/BOOTAA64.EFI` til samme korts
   `EFI/BOOT/BOOTAA64.EFI`. Det er et diagnosekort, ikke det gamle diskimage.
3. Kontroller de kopierede filers hashes igen og skub mediet sikkert ud.
4. Alternativt: firmware-SD plus særskilt disponibelt FAT32-USB-probemedie,
   hvis firmwaren kan registrere det. Dokumenter den præcise konfiguration.

Rå image-skrivning er ikke nødvendig til filkopieringsproceduren.
[Windows-værktøjet](scripts/pi5_hw/write_image_safe.ps1) er kun til udtrykkeligt
valgte image-skrivninger: angiv `-Image` og `-ExpectedSha256`, da standarderne henviser
til historiske artefakter. `-TargetFile <almindelig-fil> -Execute` øver uden disk.
Den fysiske rå-disk-sti er fortsat **ikke afprøvet/verificeret**.
Kopier aldrig andres disknumre eller `dd`-kommandoer.

## 5. UART og firmwareopstart

- Verificer det dedikerede Pi 5-debugstiks pinout og **3,3 V logik** mod
  [officiel Raspberry Pi-dokumentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html).
  Projektets tidligere kildehentning verificerede ikke pinout.
  Tilslut aldrig 5 V eller adapterens strømledning; gæt ikke stikretningen.
- Brug Raspberry Pi Debug Probe eller dokumenteret USB-UART-adapter.
  Fælles jord og krydsede RX/TX efter det verificerede pinout.
- Optag ved **115200, 8N1, ingen flow control**, før strømmen tændes:

```powershell
python tests\pi5_hw_validation\capture_serial.py --list
python tests\pi5_hw_validation\capture_serial.py --port COM5 --seconds 180 --label pi5-probe
```

Installer om nødvendigt pyserial 3.5 i eget isoleret miljø. Linux bruger den
faktiske `/dev/ttyUSB*`- eller `/dev/ttyACM*`-sti i stedet for `COM5`.
Logs og SHA-256-sidefiler gemmes i `logs/pi5_hardware`. Værktøjet sender ingen
payloadbytes, men åbning af en serialport kan ændre adapterens kontrollinjer.

Vælg i UEFI: **Device Manager → Raspberry Pi Configuration →
ACPI / Device Tree → System Table Mode → Device Tree**.
Standard-ACPI leverer ikke probens krævede FDT. Registrer version og alle
indstillinger. `Esc` åbner setup. Brug om nødvendigt UEFI-shell, `map -r`,
find det faktiske `fsX:` med `EFI\BOOT\BOOTAA64.EFI`, og start filen eksplicit.
Firmwaremenu eller shell er kun en firmwareobservation.

## 6. Klassificer proben, ikke hele kortet

```powershell
python tests\pi5_hw_validation\evaluate_probe_log.py --mode pi5 logs\pi5_hardware\YOUR_CAPTURE.serial.log
```

Gem `PI5PROBE BEGIN`, revision, root compatible, alle `PI5PROBE CHECK`,
UART-adresse, FDT-controllerbeskrivelser og `PI5PROBE END`.
`PI5_PROBE_PASS` betyder, at den indsendte logs markører passerede parseren;
det er ikke uafhængig autentifikation, periferi-funktionalitet eller samlet
HARDWARE_PASS. Manglende/tom fysisk log er BLOCKED_BY_PHYSICAL_HARDWARE.
Bevar hele loggen også ved parserfejl. Brug ikke `--mode qemu` til fysiske påstande.

Probe-evaluator afstemmer dublerede checks og END-tællere/resultat, kræver
Pi 5-root-compatible og den præcise numeriske debug-UART-adresse samt afviser
modstridende krævede checks. Kort/polstret hexadecimal og store/små bogstaver
accepteres. De to Pi-specifikke discovery-checks er kun informative i generisk
QEMU-mode. Syntetiske logs kan passere en parser, også den gamle evaluator;
det er ikke autentisk fysisk evidens. Kontroller alle checks manuelt og bevar
hele den originale optagelse.

PCIe/RP1 beskrevet i FDT er kun discovery-metadata. Fysisk link, BAR-mapping,
MSI-X, USB-registrering og Ethernet-trafik kræver særskilte implementerede
native test og observationer.

## 7. Individuelle subsystemer

| Subsystem | Baseline i producentens OS | Aktuel RROS-validering |
|---|---|---|
| USB | `lsusb -t`, tastatur/mus/disponibelt drev, `dmesg` | Controller-/clock-handoff ufuldstændigt; native drift NOT_TESTABLE |
| Ethernet | `ip link`, `ethtool <faktisk-interface>`, link/PHY/DHCP | GEM-produktionsopstart mangler; firmwarebegrænsning; NOT_TESTABLE |
| Skærm | `kmsprint` eller installeret `modetest`, EDID, skærm/mode/kabel | Firmwarebillede er ikke RROS-modesetting; native HDMI NOT_TESTABLE |
| Kamera | `rpicam-hello --list-cameras`, modul og Pi 5-kabel | Ingen capture-pipeline; NOT_TESTABLE |
| Lyd | `aplay -l`, præcis ekstern DAC/overlay/output | Ingen produktions-I2S/DMA; Pi 5 mangler analogt jack; NOT_TESTABLE |
| Wi-Fi/Bluetooth | `iw dev`, `bluetoothctl show`, firmwarenavne/hashes | Ingen trusted bundlet firmware/operativ stack; NOT_TESTABLE |
| Lager | Medieidentitet, mounts og read-only-baseline | Ingen verificeret native Pi-lager; skriv ikke på værdifulde medier |
| GPIO/I2C/SPI | Device-/kabel-/spændingsdokumentation og DT | Kun fixtures; ingen aktuatorer eller ukendte pin-signaler |

Manglende valgfrie baselineværktøjer skal rapporteres, ikke erstattes med
opfundet output. Aktiver ikke overlays, netværkstrafik eller GPIO uden forstået
og godkendt effekt. Fremtidig RROS-PASS kræver præcis testkommando,
driverdiagnostik og fysisk observation. Stabilitetstid og temperatur kan
rapporteres som observationer, ikke benchmarks eller produktgarantier.

## 8. Fejlfinding, recovery og rapport

Intet output: kontroller strøm, sikker ledningsføring, adapter, baud,
optagelse-før-boot, FAT32-filer og Device Tree. Delvist output: gem sidste
markør, alle FAIL-checks og END-tællere. App ikke fundet: kontroller mediemapping
og EFI-fallbacksti. D0/skærm: læs upstream-begrænsninger; sort skærm er ikke
nødvendigvis en kernefejl.

Sluk, fjern testmediet og indsæt det kendt fungerende baselinekort.
EEPROM-recovery/opdatering er en separat ejergodkendt producentprocedure.
Indsend [rapporten](COMMUNITY_TEST_REPORT_TEMPLATE.md) via
[issueformularen](.github/ISSUE_TEMPLATE/pi5-hardware.yml): hardware,
firmware/indstillinger, release/SHA-256, hvert resultat, UART/hash, diagnostik
og eventuelle ægte fotos/video. Mærk baseline, firmware, probe og native RROS
separat. Fjern følsomme oplysninger før offentlig upload.
