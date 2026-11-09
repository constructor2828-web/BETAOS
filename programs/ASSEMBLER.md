# Ensamblador integrado de BETAOS

F6 abre el editor. El ejemplo inicial está listo para ejecutarse con **Ctrl-R**.
Ctrl-S guarda el archivo actual; Ctrl-Z/Ctrl-Y deshacen y rehacen hasta 16 cambios.
Las flechas mantienen la columna al cambiar de linea; Inicio/Fin actuan sobre la
linea y Ctrl-Inicio/Ctrl-Fin sobre el documento. Antes de perder cambios sin guardar
puedes guardarlos, descartarlos o cancelar.

Ctrl-B ensambla e instala el programa `scratch`.
El archivo puede crearse desde el menú contextual del escritorio y abrirse con
doble clic. También puedes usar `edit archivo.asm`, seguido de `asm archivo.asm app`.

El ensamblador y el cargador funcionan dentro de BETAOS. No necesitan GCC, NASM,
Python, Linux ni otro compilador externo en el SO.

```asm
; Hola mundo nativo, sintaxis Intel
bits 64
start:
    push rbx
    mov rbx, rdi
    lea rdi, [rel mensaje]
    call [rbx+8]
    pop rbx
    ret
mensaje: db "Hola desde ASM!", 10, 0
```

El resultado es un ejecutable BEX v1. `start` es la entrada si existe; si no, la
entrada es el primer byte del payload. Los datos y las instrucciones comparten
el mismo bloque; utiliza etiquetas y RIP relativo para referenciar cadenas.

## Subconjunto implementado

- Registros `rax rcx rdx rbx rsp rbp rsi rdi r8` a `r15`.
- `mov REG, REG` y `mov REG, INMEDIATO` de 64 bits.
- `add`, `sub`, `cmp` entre registros o con inmediato de 32 bits con signo.
- `xor REG, REG`, `inc REG`, `dec REG`, `push REG`, `pop REG`, `nop`, `ret`.
- `lea REG, [rel etiqueta]`.
- `jmp`, `jnz`, `jz`, `call` a etiquetas, con desplazamiento relativo de 32 bits.
- `call [rbx+8]`: imprimir cadena NUL terminada (`rdi` apunta al texto).
- `call [rbx+16]`: imprimir entero decimal de 64 bits (`rdi` contiene el valor).
- `call [rbx+24]`: obtener MiB disponibles al arrancar (`rax` recibe el resultado).
- `db` con números decimales/hexadecimales y cadenas, separados por comas.
  Cadenas con `\n`, `\r`, `\t`, comillas escapadas y barras invertidas.
- `bits 64`, comentarios `;`, etiquetas y números negativos; hexadecimal `0x...`.

No hay macros, includes, secciones, expresiones aritméticas, registros de otros
anchos, instrucciones SIMD ni sintaxis NASM completa. Mantén `rel` en minúsculas.
Se admiten 128 etiquetas, 128 referencias y líneas de menos de 512 caracteres.
El editor tiene 8191 bytes y el payload ejecutable no puede superar 65536 bytes.

## ABI

Al entrar, `rdi` apunta a la tabla de API descrita en [README.md](README.md).
`rbx`, `rbp`, `r12`–`r15` deben conservarse. Alinea la pila a 16 bytes antes de
llamar a una función. Una forma sencilla para el ejemplo es `push rbx` al entrar
y `pop rbx` antes de `ret`. Los callbacks pueden modificar registros temporales;
guarda la tabla en un registro preservado.

Los programas se ejecutan directamente en modo kernel, sin aislamiento ni límite
de tiempo. Un bucle infinito, un retorno incorrecto o un acceso inválido puede
bloquear el SO. Las apps instaladas y el código fuente guardado se conservan en
un disco BETA instalado; sin él, solo viven en RAM.
