# 06: Fósforo (decay persistente)

**What to build:** El look del CRT: al componer cada campo, el contenido previo se desvanece con una persistencia tipo fósforo P39 (ghosting al hacer scroll). Es la firma visual del terminal B/N y reemplaza el "pixel perfect".

**Blocked by:** 03

**Status:** resolved

- [x] Al hacer scroll rápido, el contenido anterior "fantasmea" unas décimas de segundo y se desvanece con persistencia tuneable.
- [x] No degrada el frame: la composición por campo sigue dentro del presupuesto, sin tearing.
- [x] Desactivable por `config.h` (vuelve a "pixel perfect").

## Comments

### Implementado (2026-08-30)

- **Blend puro en `src/phosphor.h`** (seam de testing host):
  `Phosphor::blend(glow, fresh, decay)` = si `fresh != 0` re-excita a su nivel,
  si no el glow decae por `decay` (fixed-point 0..256). Testeado en
  `tests/phosphor/phosphor_test.cpp` (re-excitación, decaimiento geométrico,
  monotonicidad, decay 0/256, aplicado por fila).
- **Integración:** `renderTarget` (frame fresco donde pintan LVGL y los glifos)
  separado del buffer del driver (`fb`, el glow). Cada campo, `phosphorFrame()`
  pliega `renderTarget` sobre `fb` con `TERM_PHOSPHOR_DECAY` (224 ≈ 0.875/field,
  ghost de unas décimas de segundo a 60 campos/s). El driver compone desde `fb`
  sin tocarlo (copia verbatim intacta).
- **Config:** `TERM_PHOSPHOR` (1 = fósforo, 0 = pixel-perfect) y
  `TERM_PHOSPHOR_DECAY` (0..256, tuneable).
- **Sin tearing:** el plegado corre en el loop después de renderizar y antes de
  que el driver componga el campo en el EOF; el driver nunca lee mientras el
  app escribe.
- **Build/upload:** host tests + ASAN verdes; `bash flash.sh` OK (45% flash /
  74% RAM con SSH activo).