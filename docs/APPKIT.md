# Crear aplicaciones para USB

El kit utiliza GNU binutils en el equipo de desarrollo. El resultado es código
máquina x86-64 con cabecera BEX, ejecutado directamente por BETAOS.

```sh
python3 tools/appkit.py new build/miapp
# Edita build/miapp/main.S
python3 tools/appkit.py build build/miapp
```

Copia `build/miapp/miapp.bex` a un pendrive FAT16 o FAT32 desde tu equipo.
Expúlsalo normalmente antes de conectarlo a BETAOS. En la terminal del SO:

```text
usb refresh
usb ls
usb install miapp.bex
run miapp
```

También puedes seleccionar el BEX en Archivos → USB y pulsar Instalar. La
aplicación instalada aparece en Inicio. BETAOS lee el USB; no escribe ni formatea
el dispositivo. El kit no toca dispositivos físicos ni necesita recompilar la ISO.

`main.S` usa sintaxis GNU AT&T. El ensamblador integrado del editor utiliza el
subconjunto Intel documentado en [ASSEMBLER.md](../programs/ASSEMBLER.md); no son
fuentes intercambiables. Ambos producen el mismo formato BEX.

Dentro de BETAOS, F6 abre el editor: Ctrl-F busca (Enter encuentra la siguiente
coincidencia, Esc vuelve a editar), el ratón coloca el cursor, Enter conserva
la sangría y Ctrl-Z revierte una inserción completa. Ctrl-B compila e instala
con el nombre del archivo sin extensión; Ctrl-R además ejecuta la aplicación.
Ctrl-S guarda el fuente. Los límites siguen siendo 8191 bytes por fuente y
64 KiB por ejecutable. Los programas se ejecutan en modo kernel, sin aislamiento.

Prueba reproducible: `python3 tests/appkit_workflow.py`. Crea un proyecto nuevo,
lo compila, lo coloca en una imagen FAT de prueba y lo instala y ejecuta mediante
el controlador USB nativo en QEMU. No utiliza el disco personal del usuario.
