# SSTV módok

Az adóforrás és a képelőkészítő 40 módot kezel. A „validált” módokat sikeresen
dekódolták SSTV-vevővel.

Az adóoldali próbákat Raspberry Pi 2 Model B+ hardveren végeztük.
Az adásokat Gqrx → QSSTV vételi láncon ellenőriztük, a QSSTV Auto Slant
beállítását bekapcsolva. Vevőként RTL-SDR-t és RSP1A-klónt használtunk.

## Első teljes validáció (2026-10-15)

39 módot teszteltünk: 35 `valid_readable`, 4 `FAIL`. A `FAX480` kimaradt a körből. A javítás alatt álló öt mód és a sikeres módok listája: [rövid validációs összefoglaló](VALIDATION-SUMMARY.md).

**Validált:** `M1`, `M2`, `S1`, `S2`, `DX`, `SC2-60`, `SC2-120`, `SC2-180`, `P3`, `P5`, `P7`, `MP73`, `MP115`, `MP140`, `MP175`, `MR73`, `MR90`, `MR115`, `MR140`, `MR175`, `ML180`, `ML240`, `ML280`, `ML320`, `ROBOT8`, `ROBOT24`, `ROBOT36`, `ROBOT72`, `BW12`, `PD50`, `PD90`, `PD120`, `PD180`, `PD240`, `PD290`.

**Javítás alatt / új validálás szükséges:** `AVT24`, `AVT90`, `AVT94`, `PD160`, `FAX480`.

A FAX480 nem volt része az első 39 módos körnek. Az előzetes próbája nem adott mentett vételi képet, és adás után maradék RF-jelet hagyott; további próba előtt a leállítási útvonalat kell ellenőrizni. A mitigáció után mind a 40 módot újra kell validálni.

## Képméretek

| Módcsalád | Méret |
|---|---:|
| M1, M2, S1, S2, DX, SC2-60/120/180, PD50, PD90 | 320×256 |
| Robot8, Robot24, BW12 | 160×120 |
| Robot36, Robot72, AVT90 | 320×240 |
| AVT24 | 128×120 |
| AVT94 | 320×200 |
| PD120, PD180, PD240, P3, P5, P7, ML180/240/280/320 | 640×496 |
| PD160 | 512×400 |
| FAX480 | 512×500 |
| MP73/115/140/175, MR73/90/115/140/175 | 320×256 |
| PD290 | 800×616 |
