# Használati útmutató

A PiSSTV SSTV-képet állít elő és rádiófrekvenciás adást indít. Csak előkészített, megfelelően csatlakoztatott és a kezelő által ellenőrzött tesztállomáson futtasd.

## Telepítés és fordítás

A Raspberry Pi OS-en futtasd a repó gyökeréből:

```sh
sudo ./install.sh
```

Ez telepíti a buildfüggőségeket, letölti és telepíti a rögzített `librpitx` verziót, majd lefordítja a programot. Már telepített `librpitx` mellett a `make` újrafordítja a programot. A futtatható fájl a `build/pisstv`.

## Kép előkészítése

A képelőkészítő létrehozza az SSTV-mérethez igazított PNG előnézetet és a fejléc nélküli RGB bemenetet:

```sh
python3 tools/prepare_sstv_image.py input.jpg --mode PD180 --title "Teszt"
```

A példa `input_pd180.png` és `input_pd180.rgb` fájlt hoz létre. Az RGB fájlt ne szerkeszd utólag; a képméretet a kiválasztott mód határozza meg.

## Adás indítása

```sh
sudo build/pisstv input_pd180.rgb 433100000 PD180
```

A paraméterek sorrendje: RGB fájl, középfrekvencia Hz-ben, opcionális mód. Mód nélkül `M1` az alapértelmezett. A program indulása adást indít; a `Ctrl+C` leállítja az aktuális folyamatot.

Hosszú hullámforma-előkészítésű módnál az RF-indítást ideiglenesen kapuzhatod. A hullámforma előbb elkészül, miközben az RF még nem indul:

```sh
sudo env PISSTV_HOLD_BEFORE_RF=1 build/pisstv input.rgb 433100000 P3
```

A kiírt PID-hez küldött `SIGUSR1` indítja az RF-adást; `Ctrl+C` megszakítja. Ezt csak akkor használd, ha a futó program kiírta, hogy készen áll és az RF még ki van kapcsolva. Ha a program nem jelez ilyen kaput, ne küldj jelet találomra.

## Rövid középfrekvenciás vivőpróba

```sh
sudo build/pisstv --baseband-zero 433100000
```

Ez 10 másodperces RF-próba, nem néma szoftverteszt.

## Módok és validáltság

A program 40 módnevet fogad. A megvalósított módok felsorolása és a rövid, módonkénti validációs állapot a [módlistában](MODES.md), a részletes összegzés pedig a [validációs kivonatban](VALIDATION-SUMMARY.md) található. A `FAIL` jelű módokat ne tekintsd működőnek; javításuk és teljes körű újravalidálásuk még szükséges.
