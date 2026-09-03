# 0002 Autenticación SSH por password en el CRT

El terminal autentica contra el host **tecleando el password en cada conexión**, desde el teclado BLE, mostrado enmascarado en el CRT. Sin clave privada almacenada ni portafolio de credenciales.

Se descartó clave privada en flash a pesar de que el dispositivo es personal y de config fija (ADR-0001): el flujo de arranque queda "sin estado" (no hay secreto que gestionar ni rotar) y la pulsación en CRT es la experiencia deseada. Un lector futuro que añada un key agent estará añadiendo gestión de secretos que esta decisión eliminó a propósito.