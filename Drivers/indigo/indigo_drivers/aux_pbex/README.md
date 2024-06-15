# BigPowerBox driver

https://github.com/MichelMoriniaux/BigPowerBox

## Supported devices
* BigPowerBox

## Supported platforms

This driver is platform independent.

## License

INDIGO Astronomy open-source license.

## Use

indigo_server indigo_aux_pbex

## Status: Stable

Tested with physical device.

## Build instructions
* this driver depends on indigo server. The indigo server code can be downloaded from the github link below -

https://github.com/indigo-astronomy/indigo/tree/master

Alternatively, the binaries are available here -

https://www.indigo-astronomy.org/downloads.html

Additionally, gcc compiler and other build toold are also required.

Once the indigo environment is setup, follow these steps to build the indigo_aux_pbex (BigPowerBox) driver -
* start by compiling indigo_drivers/aux_pbex/indigo_aux_pbex_main.c

  gcc -c indigo_aux_pbex_main.c

  if instead of binaries indigo server code was built then, libindigo.so needs to be added to the LD_LIBRARY_PATH.

* build a shared library

  sudo gcc -fPIC -shared -o /usr/lib/indigo_aux_pbex.so indigo_aux_pbex.c

  by default libindigo exists in /usr/lib/. If the code was built from scratch then the relative library path would be /indigo/build/drivers.

* build executable 

  sudo gcc -o /usr/bin/indigo_aux_pbex indigo_aux_pbex_main.o -l:libindigo.so -l:indigo_aux_pbex.so -lm

* run/load the driver 

  if indigo server was installed directly just run -
  indigo_server indigo_aux_pbex

  if indigo server is built from scratch load the driver either from indigo control panel or from the indigo config web page.
    