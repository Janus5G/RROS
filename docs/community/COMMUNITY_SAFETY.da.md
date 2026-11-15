# Sikkerhedsvejledning til communityets hardwarearbejde

[English](COMMUNITY_SAFETY.md) | [Eksisterende hardwareguide](HARDWARE_GUIDE.md) |
[Sikkerhedsgate](SAFETY_GATE.json) | [Dokumentationsskabelon](EVIDENCE.md)

Vejledningen forklarer den eksisterende gate; den opretter ikke nye
testgates og godkender ingen procedure. Den nuværende gate godkender
**ingen fysiske operationer**. De hentede `1.0.0-dev.2`-filer og PDF'en er
historiske øjebliksbilleder; dette dokument supplerer dem uden at ændre
releasefilerne. Ingen formulering her fastslår, at en
konfiguration er sikker.

## Resultatbetegnelser

Registrér resultatet for hvert eksisterende krav og den præcise konfiguration:

- **PASS**: acceptkriterierne for netop denne kontrol er opfyldt og underbygget
  af kontrollerbar dokumentation. Gennemgang eller software-PASS gælder kun
  det angivne omfang og giver hverken tilladelse til næste trin eller hardware-PASS.
- **FAIL**: et kriterium er ikke opfyldt, der er fundet en modstrid, eller en
  test har givet et uacceptabelt resultat. Stop, og gem dokumentationen.
- **BLOCKED**: operationen kan ikke fortsætte, fordi en forudsætning, godkendt
  procedure eller tilladelse mangler, eller en relevant kritisk gate har
  FAIL, BLOCKED eller NOT_VERIFIED.
- **NOT_VERIFIED**: den nødvendige observation, måling, gennemgang eller
  dokumentation er ikke gennemført eller accepteret. Antag ikke succes.

Projektets overordnede status skelner mellem SOFTWARE_PASS og
HARDWARE_NOT_VERIFIED/HARDWARE_PASS. Host-, simulerings-, build- eller
kildekoderesultater kan ikke bevise HARDWARE_PASS. En indsendt måling er
dokumentation til vurdering, ikke automatisk sikkerhedsgodkendelse eller
ændring af en gate.

## Niveau A: frakoblet gennemgang

Kvalificerede bidragydere kan gennemgå følgende med alle komponenter
frakoblede og uden strøm. Brug [skabelonen](EVIDENCE.md); de eksisterende,
detaljerede kriterier er E01-E08 i [el-checklisten](ELECTRICAL_PRE_POWER.md).

| Gennemgang | Dokumentation og accept | Stop hvis |
|---|---|---|
| Stykliste og kompatibilitet | Sammenlign [BOM.csv](BOM.csv), antal, præcis boardrevision, hukommelse/stepping, tilbehør, strømforsyning og kabel med relevante producentdokumenter. Registrér dokumentversioner og afvigelser. PASS gælder kun det, der faktisk er bekræftet. | En del, revision, mærkeværdi eller kompatibilitet er ukendt eller modstridende. |
| Datablade og spænding | Registrér grænser for begge endepunkter, forsyningsprofil, GPIO/UART-logikniveauer, strøm pr. pin og samlede/portgrænser med kilder. Registrér et specificeret strømbudget. PASS kræver dokumenterede, kompatible værdier og marginer. | En grænse, belastning, polaritet eller margin mangler, er uforenelig eller gættet. Datablade erstatter ikke målinger. |
| Ledninger, polaritet og stik | Gennemgå en foreslået, ikke tilsluttet pin-til-pin-tabel mod præcise tegninger: pin 1, synsretning, signaler, reference, strømledere og risiko for bagstrøm. En uafhængig kvalificeret person skal kontrollere. | Retning, signal, polaritet eller kontrol er usikker; tilslut ikke UART, GPIO eller strøm. |
| Samling og køling | Inspicér frakoblede komponenter, monteringsafstande, blottede ledere, snavs og risiko for kortslutning; sammenlign køler-/kabinetvejledninger. Registrér redigerede fotos og afvigelser. | Skade, metalkontakt, utilstrækkelig afstand eller usikkerhed. Et billede beviser ikke korrekt montering. |
| Frakoblet kontinuitet/kortslutning | En kvalificeret ingeniør skal først definere en særskilt procedure for frakoblet kontrol med målepunkter, egnet instrument og acceptgrænser; den skal gennemgås uafhængigt (E05/E07). Først derefter kan en kvalificeret bidragyder udføre den særskilt tilladte frakoblede kontrol. Registrér instrument, forhold, målinger og fortolkning. | Ukendt lagret energi, blotlagt forsyning eller anden kilde, usikre målepunkter, manglende procedure/grænser eller uventet resultat. Brug ikke kontinuitetsmåling på et strømførende kredsløb; et bip beviser hverken sikre strømføringer eller en sikker konfiguration. |
| Kildekode og hostede softwaretests | Brug de eksisterende [softwarekommandoer](RELEASE.md#supported-software-checks) og [reviewresultater](SAFETY_REVIEW.md). Registrér commit, værktøjsversioner, præcise kommandoer og output. PASS gælder kun det faktiske host- eller simulationsmiljø. | En krævet test fejler, ikke kan køre eller mangler dokumenteret ophav; kald ikke manglende resultater PASS. |

Dokumentation fra frakoblet inspektion giver ikke tilladelse til tilslutning,
strømførende målinger, opstart, skrivning til medier eller fysisk RROS-test.
Denne vejledning opfinder ingen pinnumre, strømgrænser eller accepttærskler.

## Niveau B: betinget elektrisk verifikation

Ingen procedure for strømførende målinger er aktuelt godkendt af denne
vejledning eller [gaten](SAFETY_GATE.json). Før en kvalificeret bidragyder
udfører en særskilt foreslået elektrisk måling, skal følgende dokumenteres
for den **præcise konfiguration og målingens omfang**:

1. Relevant E01-E08-dokumentation er komplet, afvigelser er afklaret, delene
   identificeret, og en anden kvalificeret person har kontrolleret pinout,
   polaritet, spændingskompatibilitet, strømbudget, ledninger og bagstrøm.
2. En kvalificeret el-ingeniør har defineret en afgrænset, boardspecifik
   procedure med udstyr, korrekt klassificeret instrument og måleområde,
   målepunkter, isolation, grænser, usikkerhed, stopbetingelser og
   genopretning. En reviewer skal godkende proceduren uafhængigt **før**
   udførelse; relevante tilladelser fra ejer og ansvarlige for stedet og
   udstyret kræves.
3. Proceduren angiver præcist, hvad der eventuelt må være strømførende,
   strømgrænserne og status for relevante forudsatte gates. De eksisterende
   blokeringer af `power-up` og tilslutning tilsidesættes ikke: opstart af et
   samlet system eller UART/GPIO-tilslutning kræver en særskilt godkendt
   procedure og opdateret status for relevante gates.
4. Registrér konfiguration, dato, procedureversion, udstyr/instrument og
   måleområde, forhold, punkter, værdier og enheder, usikkerhed, relevante
   acceptgrænser, uventede observationer og redigerede fotos. Vurdér mod de
   fastlagte grænser; ændr dem ikke efter at have set resultaterne.
5. En kvalificeret ingeniør, der er **uafhængig af den, der målte**,
   gennemgår resultater og resterende risici. Projektets safety-gate-reviewer
   registrerer relevant gate-status og godkender udtrykkeligt den næste
   afgrænsede operation. Bidragyderens resultat gør ingen af delene automatisk.

Stop ved manglende godkendelse, tvetydige ledninger/grænser, uventede
målinger, varme, lugt, skade, reset, overstrøm eller et fejlet kritisk krav.
Checklisten indeholder ingen instruktioner til strømførende ledningsarbejde
eller opstart. Niveau B giver ikke automatisk tilladelse til opstart af det
samlede system eller til at køre RROS.

## Niveau C: aktuelt blokerede operationer

Tabellen angiver **nødvendig dokumentation**, ikke tilladelse til at udføre
den blokerede test for at skaffe den. Enhver foreslået indsamling af
dokumentation kræver selv opfyldte forudsætninger, en uafhængigt gennemgået
procedure og relevante tilladelser. En kvalificeret ingeniør og projektets
safety-gate-reviewer skal vurdere dokumentationen og udtrykkeligt godkende
hver næste operation. Hardwareafhængig dokumentation kan forblive NOT_VERIFIED,
indtil en sådan særskilt operation er godkendt.

| Blokeret operation | Uafklaret risiko / eksisterende krav | Nødvendig dokumentation før operationen kan overvejes |
|---|---|---|
| Native RROS- eller probe-opstart | Generisk AArch64-entry er QEMU-orienteret; fysisk entry, handoff, firmware/medie og elektrisk konfiguration er ikke verificeret: `TARGET-ENTRY`, `FIRMWARE-MEDIA`, `ELECTRICAL`. | Gennemgået Pi-specifikt entry/handoff, præcis board-/firmware-/mediekompatibilitet og hashes, afsluttet el-gennemgang samt særskilt godkendt boot-/medieprocedure. |
| GPIO/MMIO-adgang | BCM2712/RP1-mapping, gyldige registre, adgangsbredder, barrierer og elektrisk pin-konfiguration er ikke verificeret: `MMIO`, `ELECTRICAL`. | Uafhængigt gennemgået boardspecifikt registerkort, mapping/bredde/rækkefølge og pin-konfiguration; afsluttet el-dokumentation; en afgrænset adgangsprocedure og relevant forudgående hardwaredokumentation. |
| UART-tilslutning | Stikretning, begge endepunkters spænding, reference, polaritet, bagstrøm og levering er ikke verificeret: `ELECTRICAL`, `MMIO`. Software-PASS for videresendelse beviser ikke fysisk UART. | Uafhængigt gennemgået præcis stik-/pin-tabel og adapterspecifikationer, kompatible niveauer målt efter godkendt procedure, bagstrøms-/strømgennemgang og særskilt godkendt tilslutnings-/opsamlingsprocedure. |
| DMA/cache-koherenstest | Fysisk DMA-adresseoversættelse, alignment, ejerskab/levetid, synkronisering, cachevedligehold og rækkefølge er ikke verificeret: `IRQ-DMA`, `MMIO`. Serialisering af allokatoren beviser ikke, at en enhed er standset. | Gennemgået platformens DMA-mapping og cache-/rækkefølgedesign, dokumenteret kontrolleret ejerskab og standsning, nødvendige target-/register- og el-forudsætninger samt særskilt godkendt afgrænset DMA-procedure. |
| Interrupts og eksterne enheder, herunder PCIe/RP1, USB, Ethernet, storage, skærm, kamera, lyd og trådløst | Register-/interruptkonfiguration, firmware, strøm, enhedsadfærd og eventuelt DMA er ikke verificeret: `MMIO`, `IRQ-DMA`, `ELECTRICAL`, `FIRMWARE-MEDIA`. | Board- og enhedsspecifik gennemgang af registre/interrupts/ressourcer, kompatibel firmware og strøm, nødvendig DMA/cache-dokumentation samt særskilt godkendelse af hver enhedsprocedure. USB har også analyzer-advarslen nedenfor. |
| Firmwareopdatering, medieskrivning, strømsekvens eller anden konfigurationsafhængig operation | Kompatibilitet for board/firmware/medie, distributionsrettigheder, destruktive følger, strøm og præcis konfiguration er uafklaret: `FIRMWARE-MEDIA`, `ELECTRICAL` og eventuelt `TARGET-ENTRY`. | Præcis kompatibilitet og artefakt-hashes, relevant rettighedsafklaring, backup og udtrykkelig tilladelse til destruktive operationer, gennemgået strømsekvens og særskilt godkendt procedure. |
| Robotaktuering | Hardware, strøm, udgange og eksterne belastninger er ikke verificeret; der findes ingen aktuator-sikkerhedsgennemgang eller procedure. | Uafhængig elektrisk og mekanisk risikovurdering på systemniveau, isolation og nødstopdesign, alle forudsatte enhedsgates og udtrykkelig operationsspecifik tilladelse. |

De eksisterende SW-01 til SW-05 og HW-01 til HW-03 står fortsat i
[HARDWARE_GUIDE.md](HARDWARE_GUIDE.md#6-test-gates-and-expected-results).
Tidligere private dev.2-resultater dokumenterer PASS for community package
CI, native ARM64 software-CI og CR-01–CR-07 ved commit
`df8e20e76580898c57a25a98049aa72419b9a86e`; de historiske kørselsrapporter
følger ikke med denne distribution, og ingen af resultaterne er fysisk
Pi-dokumentation. Denne nye distribution har ikke kørt CI. En udvidet analyse
af hele driveren rapporterede
også `core.uninitialized.Assign` i `usb_copy_bytes` langs
`os_pi5_xhci_hid_poll_report`. Advarslen er ikke relateret til de rettede
probe-/page-fejl og er stadig uafklaret. En afgrænset analyzer-PASS er ikke
et rent resultat for hele driveren. Se [SAFETY_REVIEW.md](SAFETY_REVIEW.md).
