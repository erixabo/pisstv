# SSTV módok

Az aktuális `main` adóforrás és képelőkészítő 37 módot kezel. Az AVT24/90/94
az AVT-eltávolítási feljegyzés szerint kikerült a forrásból.

## Mostani állapot — 2026-10-15

- A `main` commitja: `611d419`. Az AVT24/90/94 ki van véve.
- A PD160 javítása nincs a `main`-ben: a külön próbált változat az egész
  képet az adás előtt állítja elő, nem sorpáronként dolgozik.
- A FAX480 javítása sincs a `main`-ben: a külön próbált változat 1200 Hz-es
  sor-szinkront és normál adásvég után 250 ms várakozást használ.
- Mindkét változás egy külön próbaágban van. Annak forráshashe:
  `3f3e373904bc708049928b83e32bf83946cc644e47340447a8267324091ad060`.
  ARMv6 objektum lefordult; futtatható Pi-bináris még nincs.
- Az új, javított binárissal mind a 37 mód adásos ellenőrzése még hátravan.
  A korábbi külön próbák ezt nem helyettesítik. A telepített binárist nem
  változtattam meg.

Az adóoldali próbákat Raspberry Pi 2 Model B+ hardveren végeztük.
Az adásokat Gqrx → QSSTV vételi láncon ellenőriztük, a QSSTV Auto Slant
beállítását bekapcsolva. Vevőként RTL-SDR-t és RSP1A-klónt használtunk.

## Első teljes validáció (2026-10-15)

39 módot teszteltünk: 35 `valid_readable`, 4 `FAIL`. A `FAX480` kimaradt a körből. Ez a történeti futás még tartalmazta az AVT módokat; a későbbi eltávolítás nem írja át az eredményeit. Részletek: [rövid validációs összefoglaló](VALIDATION-SUMMARY.md).

**Az első teljes körben validált (35; történeti eredmény):** `M1`, `M2`, `S1`, `S2`, `DX`, `SC2-60`, `SC2-120`, `SC2-180`, `P3`, `P5`, `P7`, `MP73`, `MP115`, `MP140`, `MP175`, `MR73`, `MR90`, `MR115`, `MR140`, `MR175`, `ML180`, `ML240`, `ML280`, `ML320`, `ROBOT8`, `ROBOT24`, `ROBOT36`, `ROBOT72`, `BW12`, `PD50`, `PD90`, `PD120`, `PD180`, `PD240`, `PD290`.

**Történeti státusz a teljes validáció idején:** `AVT24`, `AVT90`, `AVT94`, `PD160`, `FAX480`.

A FAX480 nem volt része az első 39 módos körnek. Az AVT24/90/94 módokat 2026-10-15-én eltávolítottuk; a teljes történet és az aktuális regressziós állapot a műhelyi mitigációs jegyzőkönyvben van.

## Képméretek

| Módcsalád | Méret |
|---|---:|
| M1, M2, S1, S2, DX, SC2-60/120/180, PD50, PD90 | 320×256 |
| Robot8, Robot24, BW12 | 160×120 |
| Robot36, Robot72 | 320×240 |
| PD120, PD180, PD240, P3, P5, P7, ML180/240/280/320 | 640×496 |
| PD160 | 512×400 |
| FAX480 | 512×500 |
| MP73/115/140/175, MR73/90/115/140/175 | 320×256 |
| PD290 | 800×616 |
