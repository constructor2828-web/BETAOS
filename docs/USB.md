# Almacenamiento USB nativo

`desktop/usb.c` implementa xHCI y USB Mass Storage Bulk-Only en C independiente.
`desktop/fat.c` interpreta FAT16/FAT32 y sus directorios. El controlador inicia
despues de abandonar UEFI y no llama servicios del firmware ni utiliza Linux.

## Alcance

- Primer controlador PCI xHCI detectado; dispositivos conectados directamente a
  sus puertos raiz. Hasta cuatro unidades simultaneas.
- USB Mass Storage clase 08, subclase SCSI 06, protocolo Bulk-Only 50, LUN 0.
- Capacidad SCSI READ CAPACITY(10), sectores de 512 bytes, menos de 2 TiB.
- Unidades FAT16/FAT32 sin tabla, con particiones MBR primarias o con GPT.
- Nombres 8.3 y nombres largos, directorios, texto y vista hexadecimal.
- Solo lectura: el controlador no emite comandos SCSI de escritura ni formatea.
- Actualizar / `usb refresh` detecta nuevas conexiones. No es montaje automatico.
- Desconectar invalida la identidad del medio. Tras volver a conectar se debe
  actualizar y montar de nuevo; una seleccion anterior no usa el nuevo disco.
- Un timeout invalida el controlador hasta reiniciar para evitar reutilizar DMA
  mientras pudiera quedar una transferencia pendiente.

No implementa controladores EHCI/UHCI/OHCI, hubs, UAS, multiples LUN, exFAT/NTFS,
sectores de 4 KiB, ni teclados/ratones USB. Tampoco se instala el SO en una unidad
USB mediante el instalador SATA. El firmware puede arrancar la ISO desde una
memoria USB independientemente de estos limites del controlador nativo.

## Archivos

La lista muestra hasta 96 entradas y permite navegar hasta 15 subcarpetas.
La vista previa muestra hasta 8191 bytes de texto o los primeros 512 bytes en
hexadecimal de un binario. El archivo original puede ser mayor.
Copiar a Local admite texto Latin-1 de hasta 8191 bytes; el almacen local tiene
16 posiciones. La importacion de apps admite BEX ABI 1 hasta 65552 bytes.
El nombre de la app se toma del nombre anterior al primer punto; debe tener
1-31 letras ASCII, digitos o guiones bajos. No reemplaza una app instalada.

Las lecturas son sincronas y tienen esperas acotadas por transferencia. La ruta
de lectura comprueba limites del dispositivo/volumen, cadenas y cambios de medio.
No hay una pila USB universal ni garantia de compatibilidad fisica.

## Verificacion

`tests/usb_storage.py` ejecuta lecturas y navegacion en el kernel real, copia un
archivo local, instala y ejecuta un BEX desde Archivos, desconecta/reconecta el
dispositivo y comprueba que la imagen USB permanece identica. El backend de QEMU
tambien esta marcado como solo lectura.

`tests/usb_variants.py` cubre FAT16/FAT32, MBR/GPT, volumen sin tabla y la lectura
del mismo dispositivo USB que utilizo el firmware para arrancar.
`make test-fat` ejecuta ASan/UBSan sobre FAT y 5000 mutaciones del BPB.
`make qa` incluye estas pruebas junto con las regresiones de escritorio e instalacion.

El soporte esta validado con almacenamiento USB emulado mediante `qemu-xhci`.
No se ha conectado una memoria fisica a esta sesion.

Especificaciones utilizadas: [xHCI de Intel](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)
y [USB Mass Storage Bulk-Only de USB-IF](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
