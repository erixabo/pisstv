# Külső függőségek és licencük

## librpitx

- Projekt: [F5OEO/librpitx](https://github.com/F5OEO/librpitx)
- Jogtulajdonosi közlemény az upstream projekt szerint: Evariste Courjaud
  (F5OEO)
- Licenc: GNU General Public License, 3. verzió (GPLv3)
- A telepítő által használt upstream commit:
  `f01bc64bcdb6207f448379193bc0a8accb9aa22`
- A PiSSTV forrása a `librpitx` fejlécét használja, a program pedig a
  könyvtárhoz linkel.

A PiSSTV saját forrása a GPLv3 feltételei alatt kerül kiadásra, hogy a
`librpitx`-szel alkotott program terjeszthető legyen a függőség licencével
összhangban. A `librpitx` forrása nincs ebben a repóban; az `install.sh` a fenti
azonosítójú upstream változatot tölti le és fordítja le.

Ha a PiSSTV-vel linkelt futtatható programot továbbadod, mellékeld a GPLv3
licencet és a pontos binárishoz tartozó teljes forrást, beleértve a PiSSTV
forrását, a felhasznált `librpitx` forrást, valamint a fordításhoz szükséges
szkripteket és beállításokat. A GPLv3 a felhasználási és továbbadási jogokat
„ahogy van”, garancia nélkül biztosítja, a jog által megengedett mértékig.

Ez a fájl a függőséget és az ellenőrzött verziót rögzíti; nem helyettesíti az
upstream licencszöveget, és nem ad külön licencet az upstream kódjára.
