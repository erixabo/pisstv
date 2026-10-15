# Telepítési megjegyzések

Az `install.sh` Raspberry Pi OS-en fut. Root jogosultsággal telepíti a
fordítóeszközöket és a `librpitx` fordításához szükséges Pi userland
fejlesztőcsomagot, majd egy rögzített upstream commitból építi fel a
`librpitx`-et. Végül lefordítja és a `/usr/local/bin` alá telepíti a PiSSTV-t
és a képelőkészítőt.

Az upstream függőség az [F5OEO/librpitx](https://github.com/F5OEO/librpitx)
projektből származik. A telepítő a `f01bc64bcdb6207f448379193bc0a8accb9aa22`
commitot használja, hogy egy későbbi upstream változás ne módosítsa észrevétlenül
a függőséget.

## Kézi újrafordítás

```sh
make clean all
sudo make install
```

## Korlátok

- A program kódja közvetlenül a `librpitx` DMA kimenetét használja; az
  adóindító parancs valódi kimenetet aktivál.
- Az automatikus RF-kalibrálás és Gqrx-vezérlés nem része ennek a Pi-re szánt
  programnak.
- A telepítő letölti a `librpitx` forrását, de nem adja hozzá a PiSSTV
  forrásrepójához.
