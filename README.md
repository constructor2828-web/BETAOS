# BETAOS 0.8

Sistema operativo independiente x86-64, escrito en **C y ensamblador**. No utiliza
Linux, una distribución ni un sistema anfitrión dentro del SO. Incluye soporte C
freestanding propio y bibliotecas C embebidas para fuentes, TLS y JavaScript. Las herramientas
de compilación y Python se ejecutan únicamente en la máquina de desarrollo.

Es un prototipo funcional: arranca por UEFI, ofrece escritorio gráfico, ejecuta
programas ASM nativos y puede instalarse en un disco SATA compatible. Las pruebas
se realizan en QEMU; el hardware físico no se ha validado.

## Compilar y arrancar

Requisitos: GCC, GNU binutils con `i386pep`, GNU Make, Python 3 y mkisofs/genisoimage
con soporte EFI El Torito. Para ejecutar: QEMU x86-64 y OVMF.

```sh
make                  # build/BETAOS64.iso + SHA-256
make run              # ISO y disco virtual persistente de 128 MiB
make run-installed    # arrancar solo desde ese disco, después de instalar
```

El disco de uso personal está en `disks/BETAOS.img` y **sobrevive a `make clean`**.
El primer comando `make run` lo crea si no existe; no sobrescribe uno existente.
Los discos de pruebas están en `build/` y son desechables.

Por defecto se utiliza `/usr/share/OVMF/x64/OVMF.4m.fd`. Puedes cambiarlo mediante
`make run OVMF=/ruta/OVMF.fd`. Este archivo es la imagen OVMF combinada; las
instalaciones con archivos CODE/VARS separados requieren las opciones pflash de
QEMU. Para las pruebas, `OVMF=/ruta/OVMF.fd` es una variable de entorno.

## Escritorio

El indicador `us` de la barra cambia la distribucion del teclado al pulsarlo.
Tambien puedes usar `Ctrl-Alt-Espacio` o `keymap es` en la terminal. Se incluyen
`us`, `es`, `de` y `fr`; el ajuste se guarda si hay un disco BETA instalado.
El mapa espanol incluye la enie y AltGr para los simbolos de programacion.
El texto interno es Latin-1; los acentos muertos se insertan literalmente.

- Barra de tareas inferior con **Inicio**, apps ancladas e indicador de ventanas
  abiertas. Pulsar el icono de la app activa la minimiza; volver a pulsarlo la abre.
- Ventanas con minimizar, maximizar/restaurar y cerrar. Se arrastran por el titulo;
  cada app conserva su posicion, tamano y estado de maximizado durante la sesion.
  Las ventanas se superponen: pulsar una parte visible la trae al frente. Arrastra
  la esquina inferior derecha para redimensionar. Minimizar o cerrar revela la
  siguiente ventana; el extremo derecho de la barra muestra el escritorio.
  Alt-Tab recorre las ventanas abiertas y Ctrl-W cierra la actual.
- Doble clic en un acceso directo para abrirlo. Arrastra los iconos para moverlos.
- Botón derecho: **Nuevo archivo**, cambiar nombre, cambiar icono, organizar y
  actualizar. Cambiar icono recorre siete diseños incorporados; no importa imágenes.
- Doble clic en un archivo abre el editor. `Ctrl-S` guarda su contenido.
- Archivos incorpora Abrir, Nuevo, Nombre y Borrar. Ctrl-N crea un archivo,
  Supr solicita confirmacion para borrarlo, y RePag/AvPag recorre la lista.
- Los nombres, posiciones, diseños y archivos se conservan si existe un disco BETA
  instalado. Sin ese disco los cambios viven en RAM y se pierden al reiniciar.
- Inicio también se abre con la tecla Windows. Escape vuelve al escritorio o cierra
  el menú. El reloj usa la hora UTC del firmware; si no esta disponible muestra tiempo
  desde el arranque. Todavia no hay selector de zona horaria.

| Tecla | Aplicación |
| --- | --- |
| F1 | Navegador HTML/CSS/JavaScript y HTTPS |
| F2 | Terminal |
| F3 | Archivos y vista previa |
| F4 | Sistema y accesos a los instaladores |
| F5 | Engranajes de colores girando |
| F6 | Editor ASM/texto |

En Engranajes: espacio pausa o reanuda; `+`/`-` cambian la velocidad.

Inicio incluye un segundo panel con **todas las apps ASM instaladas**, incluidas
las compiladas en el editor y las importadas desde USB. Pulsa su nombre para
ejecutarlas y ver la salida en Terminal. Anterior/Siguiente o RePag/AvPag recorren
la lista. En Terminal funcionan `run NOMBRE` y simplemente `NOMBRE`, con
autocompletado mediante Tab. Los comandos del sistema tienen prioridad si una
app utiliza el mismo nombre; en ese caso usa `run NOMBRE`.

Puedes abrir Inicio con la tecla Windows y escribir para buscar tanto apps
instaladas como herramientas del sistema. Flechas/Inicio/Fin seleccionan y Enter
ejecuta; Escape cierra. La busqueda conserva el comando que estuvieras escribiendo
en Terminal y el filtro de Archivos. Las teclas F1-F6 siguen cambiando de app.

## Archivos y memorias USB

Archivos ahora permite buscar por nombre (`/` o Buscar), ver el tamano, copiar
con Ctrl-C y pegar una copia con Ctrl-V. Pegar genera un nombre nuevo y conserva
el original. Mantiene abrir/editar, crear, renombrar y borrar con confirmacion.
El almacen local sigue limitado a 16 archivos de texto de 8191 bytes cada uno.

El boton **Original / Nombre / Tamano** ordena la lista. La seleccion por raton y
teclado sigue ese orden; las carpetas USB aparecen primero en los modos ordenados.
Inicio/Fin saltan al primer/ultimo resultado y Ctrl-F activa la busqueda.
**Ctrl-RePag / Ctrl-AvPag** desplazan la vista previa sin cambiar de archivo.

**Ctrl-Z en Archivos Local** recupera el ultimo archivo borrado, incluso su icono
y posicion. En Terminal existe `undelete`. Este respaldo conserva un unico borrado
durante la sesion; otro borrado lo reemplaza y reiniciar lo pierde. Restaurar nunca
sobrescribe un nombre existente y requiere un hueco libre. Los archivos recuperados
se guardan en el disco BETA si esta instalado; el respaldo de borrado vive en RAM.

Los comandos protegen mejor el trabajo: `touch` conserva los archivos existentes,
`cp`/`mv` rechazan un destino ocupado y `mv` puede renombrar con el almacen lleno.
`write` y `rm` avisan si ese archivo tiene cambios pendientes en el editor.
Renombrar actualiza el nombre del editor abierto; una escritura externa sobre un
archivo sin cambios pendientes actualiza tambien su buffer.

Los botones **Local**, **USB >** y **Actualizar** seleccionan el origen, recorren
las memorias detectadas y detectan nuevas conexiones. En USB puedes abrir carpetas
con doble clic/Enter, volver con Atras/Retroceso, previsualizar texto y binarios,
copiar texto a Local e instalar ejecutables BEX mediante **Instalar**.
No sobrescribe un archivo local que ya tenga el mismo nombre.

Se ha implementado un controlador nativo **xHCI + USB Mass Storage Bulk-Only**,
con lectura **FAT16/FAT32**, MBR, GPT, nombres largos y subcarpetas. Se ejecuta
despues de ExitBootServices: no depende de que el firmware lea la memoria.
Las unidades USB se abren en **solo lectura**. La vista previa lee como maximo
8191 bytes; la importacion BEX admite hasta 65552 bytes. Copiar texto conserva
el limite del almacen local. No hay soporte exFAT, NTFS, UAS, hubs USB ni USB HID.
El codigo esta probado en QEMU; queda pendiente validarlo en hardware fisico.

```text
usb refresh
usb mount 0
usb ls
usb cd DOCS
usb cat NESTED.TXT
usb back
usb cp README.TXT
usb install FIB.BEX
FIB
```

Para probar una imagen de memoria USB dentro de QEMU, sin modificarla:

```sh
make run USB_IMAGE=/ruta/memoria.img
```

Hay mas detalles en [el documento USB](docs/USB.md).

## Instaladores y barras de progreso

Abre **Sistema → Disk Installer** o el acceso directo **Instalar SO**. Selecciona
el disco, marca la confirmación de borrado y pulsa **Instalar BETAOS**.
**Se borra todo el disco seleccionado**, incluidas sus particiones. No hay dual
boot, redimensionamiento ni instalación conservando datos anteriores.

El instalador crea GPT principal y de respaldo, una partición EFI FAT32 de 64 MiB,
copia `EFI/BOOT/BOOTX64.EFI`, verifica los bytes leídos y guarda los datos del SO.
Las barras muestran formato, copia, verificación y guardado. Necesita un disco
SATA/AHCI con sectores lógicos de 512 bytes y al menos 128 MiB. No instala en
NVMe ni en discos USB. Tras instalar en QEMU, cierra la VM y usa
`make run-installed` para arrancar sin la ISO.

La segunda partición utiliza un almacén de estado propio con CRC: conserva archivos,
programas BEX instalados y configuración del escritorio. No es un sistema de
archivos general montable desde otros SO. El guardado no es transaccional: un corte
mientras se escribe puede invalidar el estado; el cargador comprueba su CRC.
Si hay varios discos BETA, se monta el primero que detecta el controlador.

Para las apps, abre **Apps ASM** desde el escritorio/Inicio/Sistema, o escribe
`apps` en la terminal. Los botones instalan y desinstalan los paquetes locales.
Los fallos muestran un dialogo visible: por ejemplo, almacen de apps lleno o disco
de instalacion demasiado pequeno. Una operacion de apps fallida no guarda el estado.
En terminal tambien puedes usar:

```text
programs
install fib
run fib
install memory
run memory
uninstall memory
```

`hello` está instalado inicialmente. Las apps son ejecutables reales x86-64; no se
interpretan ni se simulan. Se ejecutan en modo kernel y comparten memoria con el SO.
Un fallo o bucle infinito en una app puede bloquear BETAOS; aún no existe aislamiento.

## Editor y ensamblador integrado

F6 abre un ejemplo listo para ejecutar. `Ctrl-S` guarda, `Ctrl-B` ensambla e instala
el resultado como `scratch`, y `Ctrl-R` ensambla y ejecuta. Los errores incluyen la
línea. También puedes utilizar `edit ARCHIVO` y `asm ARCHIVO NOMBRE_APP`.

El editor incorpora 16 pasos de deshacer/rehacer (Ctrl-Z/Ctrl-Y), navegacion por
lineas, Inicio/Fin de linea, Ctrl-Inicio/Ctrl-Fin del documento, sangria con Tab y
desplazamiento horizontal. Antes de cerrar o sustituir un texto modificado permite
guardar, descartar o cancelar.

El ensamblador propio soporta un **subconjunto** de sintaxis Intel x86-64, no toda
la sintaxis de NASM. Tiene registros de 64 bits, etiquetas, saltos relativos,
operaciones básicas, `lea [rel ...]`, llamadas a la API y datos `db`.
Ver [la guía del ensamblador integrado](programs/ASSEMBLER.md) y
[el formato de ejecutables y ABI](programs/README.md).

## Terminal

Historial con arriba/abajo, cursor izquierda/derecha, Inicio/Fin, autocompletado con
Tab, desplazamiento con RePág/AvPág, `Ctrl-L` para limpiar y `Ctrl-C` para cancelar
la línea. Se conserva el texto al cambiar de app. Las lineas de mas de 639 bytes
se rechazan sin ejecutar el prefijo truncado; se muestra una indicacion visible.

Comandos: `help`, `clear`/`cls`, `about`, `mem`, `net`, `echo`, `ls`/`dir`,
`write NOMBRE TEXTO`, `cat`/`type`, `rm`/`del`, `touch`, `cp`, `mv`, `wc`, `pwd`,
`history`, `uptime`, `uname`, `browse URL`, `programs`, `run`, `install`, `uninstall`,
`apps`, `edit`, `asm`, `gears`, `disks`, `installer`, `install-os`, `save`, `load`,
`reboot` y `halt`.

La terminal es un intérprete propio de comandos, **no un shell POSIX completo**:
no tiene tuberías, redirecciones, scripting ni directorios jerárquicos. `/ram` es
un espacio plano de 16 archivos de texto, hasta 8191 bytes y nombres de 31 caracteres.
Los cambios de archivos se guardan automáticamente en un disco BETA montado.
`save` guarda también las apps y el escritorio. Hay 16 espacios de apps; cada
payload nativo tiene como máximo 64 KiB.

La instalación desde terminal exige la confirmación literal:

```text
disks
install-os
install-os ERASE 0
```

Este último comando borra **el disco 0 que detecta el SO invitado**, no una letra
Windows ni un archivo indicado por ruta. Usa la ventana del instalador para ver
el modelo y capacidad del destino antes de confirmar.

## Navegador y red

El navegador incorpora **HTTPS TLS 1.2 nativo**, validacion de cadena de certificados,
nombre de servidor y fecha, HTTP/1.1, respuestas chunked y redirecciones. Usa las
raices publicas Mozilla; no desactiva la validacion para cargar una pagina.
Necesita hora UTC valida del firmware y entropia EFI RNG o RDRAND. `make run`
habilita RDRAND en la CPU virtual. No incorpora TLS 1.3.

**Ctrl-L** selecciona la barra superior para introducir una direccion o una
busqueda; Enter navega. Hay atras/adelante (Alt-Izquierda/Alt-Derecha), inicio,
recarga, cancelacion, historial de sesion y rueda del raton. Escribir
`google` o pulsar **Google** solicita `https://www.google.com/`. Escribir una consulta
solicita el buscador Google por HTTPS. La pagina inicial es una interfaz propia de
BETA Browser, con formulario hacia Google. **Ctrl-R** recarga, Tab selecciona enlaces
y controles, Enter los activa y Escape cancela una carga.

Hay un DOM propio, estilos CSS basicos y **QuickJS real**, compilado dentro del SO:
texto y colores, margenes, relleno, bordes redondeados, formularios GET, enlaces,
scripts y hojas CSS externos e imports, eventos, temporizadores, Promises y `fetch` GET del
mismo origen. CSS incluye centrado, margenes auto, porcentajes relativos al
contenedor, filas/columnas flex, variables, reglas de ancho y `!important`.
Los campos ocultos no se dibujan; los formularios GET incluyen solo controles
validos, con cursor, seleccion Ctrl-A, Inicio/Fin y borrado. La interfaz y las paginas usan fuentes proporcionales TrueType con antialiasing;
Terminal y Editor conservan la fuente monoespaciada. Se incluyen PNG, JPEG, GIF
(primer fotograma) y BMP, con transparencia y limites de memoria.
Ver [compatibilidad y limites del navegador](docs/WEB.md) y [licencias](THIRD_PARTY.md).

**No equivale a Chrome ni garantiza abrir la mayoria de webs actuales.** Faltan
partes grandes de HTML/CSS y APIs de navegador, SVG, video, cookies, modulos
JavaScript y almacenamiento web. Algunas paginas sencillas funcionan; sitios con
aplicaciones complejas pueden aparecer incompletos o fallar. Las pruebas validan
HTTPS y paginas controladas en QEMU; **la busqueda publica completa no se ha podido
validar en este entorno**, que restringe las conexiones exteriores.

La red nativa utiliza RTL8139, ARP, IPv4, DHCP, DNS A y TCP. La carga y los recursos
funcionan en segundo plano. No hay IPv6, renovacion de DHCP ni recuperacion TCP
completa. Para probar una pagina local: `python3 -m http.server 8080` en el anfitrion
y `http://10.0.2.2:8080/` en BETA Browser.

## Hardware y USB de arranque

Requiere x86-64, UEFI de 64 bits con **Secure Boot desactivado**, GOP RGB/BGR de al
menos 800×600 y teclado PS/2. El ratón PS/2 es opcional; USB HID aún no está soportado.
La red necesita RTL8139 y un servidor DHCP. El almacenamiento necesita SATA/AHCI;
no hay NVMe, Wi-Fi ni audio. 512 MiB de RAM son
recomendables. El kernel usa un framebuffer intermedio, presenta solo los
intervalos de pixeles modificados y actualiza el puntero sin repintar el escritorio.
Las interrupciones PS/2 despiertan el bucle; PIT/HLT reduce el trabajo de CPU en
reposo. Hay una ruta de sondeo si no se confirma el temporizador.

La ISO incluye arranque EFI El Torito y GPT/ESP para USB. Para usarla en una memoria
USB, escribe **la imagen completa** en modo raw/DD con un grabador de imágenes:
esto borra esa memoria. La ISO no se copia como un archivo normal. El firmware puede
arrancar desde USB. Tras salir de UEFI, la lectura de almacenamiento requiere un
controlador xHCI y un dispositivo Bulk-Only compatible; los teclados USB todavia
no tienen controlador nativo. El modo BIOS antiguo solo está disponible en el
experimento de 32 bits: `make legacy`, `make run32`, `make test32`.

## Resolucion de pantalla

Puedes guardar `BETA.CFG` en la raiz de la particion EFI:

```text
resolution=1280x800
```

El arranque elige el modo GOP RGB/BGR disponible mas cercano. Los valores validos
van de 800x600 a 4096x2160; entradas invalidas mantienen 1024x768 como preferencia.
El instalador escribe y verifica esta configuracion para conservar la resolucion
al arrancar desde el disco. Para generar una ISO alternativa sin modificar la normal:

```sh
python3 tools/profile_iso.py 800x600
```

## Rendimiento y calidad

Las mediciones locales de QEMU TCG registraron una reduccion de CPU en reposo desde
aproximadamente 100% hasta 2.5–3%. El repintado de engranajes por regiones elevo la
animacion medida de unos 53 a 57 fps y redujo su CPU activa de ~100% a ~46% en esa
misma configuracion. Son resultados de ese entorno, no garantias para todo hardware.
Ver [el informe de calidad](qa/QUALITY.md), los JSON de mediciones y el manifiesto
`qa/latest-results.json`. `perf` y `netstats` muestran contadores de diagnostico.

## Pruebas

La bateria completa requiere Pillow para las pruebas de imagenes (`python-pillow`).

```sh
make test                         # ensamblador + escritorio/ISO real en QEMU
make test-install                 # crea disco virtual, instala y arranca sin ISO
python3 tests/desktop.py --usb     # arranque GPT/USB
make qa                          # toda la bateria y 5 minutos de actividad repetida
```

Se verifican creación/renombrado/arrastre de iconos, cambios de diseño, maximizar y
restaurar, menús, apps ASM, editor y ensamblado dentro del SO, animación de engranajes,
DHCP/TCP/HTTP, confirmación de borrado, GPT/CRC, FAT32 y persistencia tras reiniciar.
Tambien se ejecutan ASan/UBSan sobre el ensamblador, cargador, protocolos y
renderizador, con mutaciones deterministas y comparaciones de repintado parcial
frente al completo. Los fallos intencionados de una app ASM verifican los marcos
de excepcion (vector, codigo, RIP y CR2): no prueban aislamiento de procesos.
Los discos de prueba están en `build/installed-os.img`; nunca se selecciona un disco
físico del anfitrión. Capturas y registros también quedan en `build/`.

Fuentes principales: `desktop/boot.c`, `gui.c`, `input.c`, `net.c`, `program.c`,
`assembler.c`, `storage.c`, `usb.c`, `fat.c`, y `desktop/cpu.S`. El SO continúa siendo un prototipo:
no hay procesos de usuario, protección de memoria, scheduler, SMP,
controladores universales ni actualizaciones de paquetes por Internet.

El arranque sigue las interfaces de [UEFI](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
y el controlador SATA sigue las estructuras [AHCI de Intel](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1_2.pdf).
