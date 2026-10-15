# AVT24, AVT90 és AVT94 eltávolítása

**Dátum:** 2026-10-15
**Státusz:** eltávolítva a forrásból és a képelőkészítőből.

A felhasználó visszaellenőrizte, hogy a QSSTV más saját adásait dekódolja, az
AVT-adást viszont nem. Mivel nincs használható független eszköz az AVT-adás
ellenőrzésére, kérte az összes AVT mód kivételét a PiSSTV-ből.

Ez az adott adás és ellenőrzés eredményét rögzíti; nem állítja, hogy a QSSTV
általában ne támogatná az AVT-vételt. A történeti validáció AVT-hibabejegyzései
változatlanok maradnak, azokat ez az eltávolítás nem írja át.

A három módhoz tartozó fejléc- és hullámforma-generálás, módválasztás,
paraméterezés, parancssori felsorolás és képméret-bejegyzés kikerült. A
képelőkészítő AVT módneveket többé nem fogad el. A 2026-10-15-i módlista így
40-ről 37-re csökkent.

A részletes, időbélyeges döntés és ellenőrzés a műhelyi mitigációs naplóban van:
`MIT-20261015-AVT-REMOVAL-02` és `MIT-20261015-AVT-REMOVAL-03`.
