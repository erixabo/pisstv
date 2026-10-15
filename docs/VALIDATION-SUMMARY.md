# Módvalidálás – rövid eredmény

**Első teljes kör, 2026-10-15:** 39 módot próbáltunk, 35 mód `valid_readable`, 4 mód `FAIL`. A `FAX480` nem volt része ennek a körnek.

- **Validált (35):** M1, M2, S1, S2, DX, SC2-60, SC2-120, SC2-180, P3, P5, P7, MP73, MP115, MP140, MP175, MR73, MR90, MR115, MR140, MR175, ML180, ML240, ML280, ML320, ROBOT8, ROBOT24, ROBOT36, ROBOT72, BW12, PD50, PD90, PD120, PD180, PD240, PD290.
- **Javítás alatt / új validálás szükséges (5):** AVT24, AVT90, AVT94, PD160, FAX480.

Az AVT24/90/94 vételi szinkronja nem állt elő; a PD160 képen jelentős sorcsúszás és slant volt. A FAX480 külön korábbi próbája nem adott mentett vételi képet, és adás után maradék RF-jelet hagyott, ezért új próba előtt a leállítási útvonalat kell javítani/ellenőrizni.

Ez tömör állapotjelentés; nem tartalmaz képet vagy helyi vételi fájlt. A 2026-10-15-i részletes, bizonyítékos jegyzőkönyv műhelyi archívumban van, és nem része ennek a Git-repónak. A teszteredmények az akkor próbált binárisra vonatkoznak; az azóta módosult forrás még nem esett át a teljes validáción. A mitigáció lezárása egy javított kiadással végzett, teljes, 40 módos újravalidálási kör.
