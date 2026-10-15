# PiSSTV

Raspberry Pi-n futó SSTV-adóprogram. A program RGB képmintát olvas, SSTV
jellé alakítja, majd a `librpitx` könyvtár `ngfmdmasync` kimenetén továbbítja.
A program meghívása tényleges adást indít.

## Követelmények

- Raspberry Pi és Raspberry Pi OS
- működő `librpitx` telepítés
- C++ fordító és Make
- Python 3 és Pillow a képek előkészítéséhez

## Telepítés

```sh
git clone https://github.com/erixabo/pisstv.git
cd pisstv
sudo ./install.sh
```

Az `install.sh` telepíti a fordítóeszközöket és a Pillow-t, letölti a
`librpitx` forrását a rögzített upstream commitból, lefordítja és telepíti a
könyvtárat, majd lefordítja a PiSSTV-t. A `librpitx` forrása külön, ideiglenes
könyvtárba kerül; nem másolja be a függőséget ebbe a repóba.

Kézi fordítás már telepített `librpitx` esetén:

```sh
make
```

## Kép előkészítése és adás

A képelőkészítő a forrásképből a módhoz illő méretű PNG előnézetet és
RGB nyers fájlt készít:

```sh
python3 tools/prepare_sstv_image.py kep.jpg --mode PD180 --title "Teszt"
```

Ez `kep_pd180.png` és `kep_pd180.rgb` fájlokat hoz létre. Az adás indítása:

```sh
sudo build/pisstv kep_pd180.rgb 433100000 PD180
```

A frekvencia Hz-ben értendő. A program képfájlt, frekvenciát és opcionális
módnevet fogad; módnév nélkül az alapértelmezett mód `M1`. A `Ctrl+C` leállítja
a futó adást.

Hosszú előkészítésű módoknál a hullámforma kiszámítása után, de az RF-modulátor
elindítása előtt kérhető hangolási kapu. Így az előkészítést a végső vevőhangolás
előtt lehet elvégezni, majd a friss hangolás után az adás azonnal indítható:

```sh
sudo env PISSTV_HOLD_BEFORE_RF=1 build/pisstv kep.rgb 433100000 P3
# Várd meg a „Waveform ready; RF is still off” üzenetet, hangolj, majd:
sudo kill -USR1 <PID>
```

A megadott PID a program saját folyamata. A kapu alatt még nincs RF-adás;
`Ctrl+C`-vel megszakítható. A környezeti változó elhagyása megőrzi a korábbi,
azonnali működést.

Rövid, 10 másodperces középfrekvenciás vivőpróba:

```sh
sudo build/pisstv --baseband-zero 433100000
```

Ez is adást indít, ezért csak a megfelelően előkészített saját tesztláncon
használd.

## Módok

**Állapot 2026-10-15:** a `main` 37 módot tartalmaz, az AVT-k nélkül. A
PD160- és FAX480-javítások még nincsenek benne; a külön próbált javításokat
és az ellenőrzés állapotát a [módlista](docs/MODES.md) írja le. A javított
37 mód teljes adásos validációja még nem történt meg.

- [Módok és validációs állapot](docs/MODES.md)
- [Rövid validációs eredmény](docs/VALIDATION-SUMMARY.md)
- [Használati útmutató](docs/USAGE.md)
- [Telepítési részletek és ismert korlátok](docs/INSTALL.md)
- [Függőségi és licencinformáció](THIRD-PARTY-NOTICES.md)

## Licenc

A PiSSTV saját forrása GPL-3.0-only licencű; a teljes licencszöveg a
`LICENSE` fájlban található. A külső függőségek licencét a saját licencükkel
kell használni.

## Kimeneti fájlformátum

Az adó bemenete tömörítetlen, sorfolytonos RGB bájtsorozat (`.rgb`), fejléc
nélkül. A képméretet a kiválasztott SSTV-mód határozza meg. A
`prepare_sstv_image.py` által készített RGB fájl ehhez a formátumhoz igazodik.
