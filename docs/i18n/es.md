# Lightshot HDR Fix

[English](../../README.md) · [Português (Brasil)](pt-BR.md) · [Español](es.md) · [Français](fr.md) · [Deutsch](de.md) · [简体中文](zh-CN.md) · [日本語](ja.md)

![Mantén HDR activado. Captura en SDR.](../assets/hero.svg)

**Mantén HDR activado. Captura en SDR.** Complemento independiente que corrige las capturas deslavadas o sobreexpuestas de Lightshot mediante FP16 y mapeo de tonos en la GPU. La imagen SDR se entrega al editor habitual, sin el borde amarillo de Windows Graphics Capture.

[**Descargar instalador para Windows**](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases/latest/download/Lightshot-HDR-Fix-Setup.exe) · [Versiones](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases) · [Validación](../VALIDATION.md)

## Instalación

1. Instala primero el [Lightshot original compatible](https://app.prntscr.com/en/).
2. Descarga y ejecuta **Lightshot-HDR-Fix-Setup.exe**. No requiere Python ni permisos de administrador.
3. El instalador detecta una instalación compatible; si no la encuentra, selecciona la carpeta de versión. Si Lightshot está abierto, pide confirmación antes de cerrarlo.
4. Finaliza y abre **Lightshot HDR Fix** desde Inicio. El acceso directo en el escritorio y el inicio al terminar son opcionales.

El instalador **0.3.0** copia Lightshot a una carpeta local de tu usuario y modifica **únicamente la copia**. El original queda intacto; no cambia el inicio de Windows automáticamente. Para volver, cierra la copia y abre el original. Usa una sola instancia: atajos, preferencias y mutex pueden compartirse.

> El instalador y el complemento no están firmados. SmartScreen puede mostrar una advertencia; comprueba que la descarga procede de los Releases de este repositorio.

## Compatibilidad y límites

Windows 10/11 y gráficos compatibles con DXGI 1.5. Requiere Lightshot x86 en la carpeta **5.5.0.7**, recurso de versión del ejecutable **5.5.0.4** y hashes exactos. Se rechazan otras versiones. **No incluye Lightshot original.** Salida SDR con HDR activado.

Pantalla completa exclusiva, contenido protegido, monitores girados y el flujo completo de edición/guardado/portapapeles necesitan más pruebas. El respaldo GDI puede seguir sobreexpuesto si falla la captura. El cursor puede duplicarse.

## Comparación controlada

![Prueba controlada de parches grises](../assets/gray-patches.svg)

Parches grises de prueba, **no una captura de juego**: salida original **210 / 255 / 255 / 255**, corregida **118 / 240 / 250 / 253**. La curva tonal cambia para separar las altas luces; no implica equivalencia universal de color.

En un único equipo RTX 5080 (3440×1440 HDR + 1920×1080 SDR): capturas completas del núcleo de **62–78 ms** en caliente y unos **312 ms** en la primera llamada. No garantiza la latencia total. [Evidencia y límites](../VALIDATION.md).

## ZIP manual y compilación

El ZIP es una alternativa avanzada que requiere Python 3.10+. Consulta los [comandos, hashes y detalles técnicos en inglés](../../README.md#manual-zip-installation).

Proyecto independiente, sin relación oficial con Skillbrains. No recompila el núcleo cerrado de Lightshot. No se ha elegido una licencia general de código abierto para el código nuevo; los componentes de terceros conservan sus licencias.
