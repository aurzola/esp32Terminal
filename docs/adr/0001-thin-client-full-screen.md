# 0001 Terminal es un thin client SSH full-screen

El terminal es un dispositivo **personal** de configuración fija (`config.h`) que muestra **una sesión** SSH a pantalla completa en un CRT B/N. Sin menú de arranque, sin multi-host, sin shell local, sin ratón: el teclado BLE es la única entrada.

Se eligió el alcance mínimo frente a las alternativas (menú multi-host, estación standalone con shell local) porque el video/LVGL/PSRAM ya acotan recursos; un menú o una shell local duplican el modelo de estados y la UI sin aportar al caso de uso original (conectarse a un host desde un CRT). Un lector futuro que asuma menú o ventanas estará violando esta decisión.