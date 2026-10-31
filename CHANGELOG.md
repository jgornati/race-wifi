# Cambios

La version del firmware es `FW_VERSION` en `RaceWifi/src/config.h`; se ve en la pantalla de
inicio y en el log serie (`[app] vX.Y.Z ...`). Cada version tiene su tag `vX.Y.Z` en git.

## 0.2.0 - 2026-10-30

- Nuevo: `TRACK_OBSTACLES` en `config.h` activa (1) o desactiva (0) los conos y neumaticos
  de la pista. Viene en **0**: la pista sale sin obstaculos. Las dos placas deben tener el
  mismo valor.
- Pruebas en PC: el error de prediccion del cliente ya no se mide durante el corte de red
  simulado, donde el cliente no recibe datos del anfitrion.

## 0.1.0 - 2026-10-30

- Primera version, probada en dos placas reales: carrera de 3 vueltas por ESP-NOW,
  manejo por inclinacion, calibracion, revancha, modo practica, diagnostico y apagado por
  BOOT o por inactividad.
