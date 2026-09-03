# 0003 La sesión es un stream de bytes ASCII/VT100

El widget de sesión consume la sesión como un **stream de bytes 7-bit + códigos VT100** (ASCII + line-drawing con el switch G0/G1 `ESC(0`/`ESC(B`). **UTF-8 queda fuera de alcance**: el host debe correr con `LANG=C` para que las cajas de `mc`/`dialog` y la interpolación de acentos se vean como el host las emite.

Se eligió el repertorio mínimo frente a decodificar UTF-8 (decode + glifos + ancho de carácter: una dimensión de trabajo completa) porque el dispositivo es personal y fijo (ADR-0001): el dueño controla la locale del host. Un lector futuro que "arregle" el render añadiendo UTF-8 estará expandiendo deliberadamente lo que esta decisión acotó.