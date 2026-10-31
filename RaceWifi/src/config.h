// Parametros ajustables del juego. Todo lo que conviene tocar al probar en hardware
// esta aca; el resto del codigo no tiene numeros magicos de afinacion.
#pragma once
#include <stdint.h>

#define FW_VERSION "0.1.0"

// ---------------------------------------------------------------- pantalla
#define LCD_W 172
#define LCD_H 320
// Reloj SPI del ST7789. El demo Arduino oficial usa 80 MHz y el demo ESP-IDF 12 MHz.
// 40 MHz es el punto medio; subir a 80 MHz si el modo diagnostico no muestra errores.
#define LCD_SPI_HZ (40 * 1000 * 1000)
#define LCD_STRIP_H 32            // la pantalla se dibuja en 10 tiras de 172x32
#define LCD_BACKLIGHT_PCT 90
// El init oficial pone el ST7789 en little-endian (RAMCTRL B0h = 00 E8), asi que el
// buffer va en uint16 nativo. Si los colores salen cruzados, poner 1: usa big-endian
// (B0h = 00 F0) e invierte los bytes de cada tira antes de enviarla.
#define LCD_BIG_ENDIAN 0
// MADCTL (36h). 0x00 es el valor del demo oficial para la vertical de 172x320.
// Correcciones si el modo diagnostico lo pide: 0x08 rojo<->azul, 0x40 espejo
// horizontal, 0xC0 rotacion 180 grados (los offsets de 34/0 son simetricos).
#define LCD_MADCTL 0x00

// ---------------------------------------------------------------- ritmos
#define SIM_HZ 60                 // paso fijo de simulacion
#define RENDER_FPS 30             // cuadros por segundo objetivo
#define NET_HZ 20                 // estados del anfitrion y controles del cliente
#define IMU_HZ 100                // lectura del QMI8658 (tarea propia)

// ---------------------------------------------------------------- inclinacion
#define TILT_DEADZONE_DEG 3.0f    // zona muerta alrededor de la posicion neutra
#define TILT_MAX_DEG 25.0f        // inclinacion que da giro maximo (sensibilidad)
#define TILT_FILTER_MS 70.0f      // constante de tiempo del filtro paso bajo
#define CALIB_WINDOW_MS 800       // tiempo quieta necesario para calibrar
#define CALIB_STILL_G 0.025f      // desvio maximo por eje (g) para considerarla quieta
#define AXIS_WIZARD_G 0.30f       // cambio minimo (g, ~17 grados) para detectar el eje
#define AXIS_WIZARD_HOLD_MS 400

// ---------------------------------------------------------------- boton BOOT
#define BTN_DEBOUNCE_MS 25
#define BTN_CLICK_MAX_MS 600      // pulsacion corta
#define BTN_VLONG_MS 2500         // pulsacion muy larga (salir / desconectar / apagar)

// ---------------------------------------------------------------- apagado
// La placa no tiene interruptor: se "apaga" durmiendola (BOOT la despierta).
#define SLEEP_IDLE_MS (5UL * 60 * 1000)  // sin uso (ni carrera ni BOOT) se duerme sola
#define SLEEP_SCREEN_MS 1500             // cuanto se ve "CHAU" antes de dormir

// ---------------------------------------------------------------- red ESP-NOW
#define ESPNOW_CHANNEL 1          // ambas placas deben usar el mismo canal
#define NET_HELLO_MS 250          // anuncio de busqueda (broadcast)
#define NET_PAIR_RESEND_MS 150
#define NET_PAIR_TIMEOUT_MS 2000
#define NET_PING_FAST_MS 200      // sincronizacion de reloj al conectar
#define NET_PING_MS 1000
#define NET_STALE_MS 350          // sin paquetes: rival en gris
#define NET_LOST_MS 2000          // sin paquetes: conexion perdida
#define NET_MAX_PACKET_AGE_MS 300 // paquetes que esperaron mas en la cola se descartan
#define RIVAL_INTERP_MS 70        // retardo de interpolacion del rival en el cliente
#define RIVAL_PROJECT 1           // adelanta el rival interpolado segun su velocidad
#define RECON_SNAP_S 64.0f        // error de prediccion que obliga a corregir de golpe
#define RECON_SNAP_X 32.0f
#define RECON_BLEND 0.25f         // fraccion del error corregida por estado recibido

// ---------------------------------------------------------------- carrera
#define RACE_LAPS 3
#define COUNTDOWN_MS 3000
#define RESUME_COUNTDOWN_MS 2000
#define FINISH_GRACE_MS 15000     // tiempo que espera al segundo tras la llegada del primero
#define PX_PER_METER 8.0f         // solo para mostrar distancias

// ---------------------------------------------------------------- fisica (px, s)
#define CAR_VMAX 230.0f
#define CAR_VMIN 40.0f            // nunca se detiene: no hay forma de quedar trabado
#define CAR_ACCEL 110.0f
#define CAR_GRASS_VMAX 100.0f
#define CAR_GRASS_DECEL 260.0f
#define CAR_LAT_MAX 140.0f        // velocidad lateral con giro maximo
#define CAR_LAT_RESP 14.0f        // respuesta lateral (1/s)
#define CAR_HALF_W 7.0f           // caja de colision del auto
#define CAR_HALF_L 13.0f
#define HIT_SPEED_FACTOR 0.35f    // choque con obstaculo
#define HIT_PENALTY_MS 600
#define HIT_INVULN_MS 1200
#define BUMP_REAR_FACTOR 0.80f    // choque entre autos: el de atras pierde mas
#define BUMP_FRONT_FACTOR 0.97f
#define BUMP_PENALTY_MS 300
#define BUMP_COOLDOWN_MS 400

// ---------------------------------------------------------------- camara
#define CAM_CAR_Y 248             // fila de pantalla del centro del auto propio
#define CAM_LOOKAHEAD 40.0f
#define CAM_FOLLOW_X 0.35f        // cuanto sigue la camara al auto al salirse del centro
#define CAM_SMOOTH_MS 250.0f
#define HUD_H 12
