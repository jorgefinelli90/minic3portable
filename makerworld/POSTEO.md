# Posteo para MakerWorld: Mini C3

Todo listo para copiar y pegar en cada campo del formulario de MakerWorld.

---

## 1. Nombre del modelo

**Español:**

```
Mini C3 – Consola de bolsillo ESP32-C3 con 9 juegos y pantalla WiFi
```

**English** (si lo publicás en inglés o agregás traducción):

```
Mini C3 – ESP32-C3 Pocket Handheld with 9 Games & Smart WiFi Display
```

## 2. Categoría y etiquetas

- **Categoría:** Hobby & DIY → Electronics. Si no aparece, Gadgets.
- **Etiquetas:**

```
esp32, esp32-c3, handheld, pocket console, oled, retro games, tetris, arkanoid, truco, smart display, wifi, diy electronics, gameboy, snap fit, no supports
```

## 3. Licencia

Sugerencia: **CC BY 4.0 (Atribución)**. Es la que más se parece a la licencia MIT del repo y permite que otros remixen dándote crédito.

## 4. Imágenes (en este orden)

1. `imagenes/portada.jpg` → **portada**
2. `imagenes/foto_mini_c3.jpg` (foto real)
3. `imagenes/juegos_menu.jpg`
4. `imagenes/juegos_jugando.jpg`
5. `imagenes/pantallas/pantallas_v120.png` (pantallas WiFi)
6. `imagenes/caja_vistas.png` (vistas de la caja)
7. `imagenes/caja_dibujo_capa1.png` (versión con dibujo a color)
8. Opcional: `imagenes/pantallas/inicio.gif` y `imagenes/pantallas/arkanoid_juego.gif`

> MakerWorld pide **al menos una foto real impresa**. La #2 sirve. Si podés, sumá fotos de la tapa abierta con la electrónica adentro y de la consola de costado: ayudan mucho a que la gente se anime a armarla.

## 5. Archivos para subir

**Modelos (STL), de `caja/stl/`:**

- `caja_frente_4botones.stl`
- `tapa.stl`
- `teclas_x4.stl`
- `teclas_x4_encastre.stl` (variante)
- `caja_frente_4botones_color.stl` + `dibujo_capa1.stl` (variante bicolor)
- `prueba_botones_y_tactil.stl`, `prueba_oled.stl`, `prueba_cierre.stl` (pruebas de calce)

**Perfil de impresión (opcional, recomendado):**

- Abrí los STL en Bambu Studio y armá una placa con **frente + tapa + teclas**.
- Laminá y subí el `.3mf` como "Print Profile". Así la gente puede imprimir con un clic.
- Si querés, sumá una segunda placa con la versión bicolor.

**Otros archivos:**

- Subí `caja/LEEME.md`, o pasalo a PDF, como guía de armado.
- El firmware va por el enlace a GitHub (está en la descripción).

## 6. Descripción (español)

> Pegá desde acá hasta el final de la sección. En el editor de MakerWorld los `##` quedan como títulos.

---

**Mini C3** es una consolita de bolsillo hecha con un **ESP32-C3-Zero**, una pantalla **OLED de 1,3"**, una cruz de 4 botones y un botón táctil. Entra en la palma de la mano: **47,5 × 86,5 × 18,6 mm**.

Trae **9 juegos**, un **anotador de truco** y, cuando no estás jugando, se convierte en una **pantalla WiFi inteligente**: hora, clima, Spotify, mensajes, noticias y mucho más.

🔗 **Firmware, código y binarios:** https://github.com/jorgefinelli90/minic3portable

## 🎮 Juegos

- **Arkanoid:** 6 niveles, ladrillos duros y cápsulas (paleta ancha, pelota lenta, multibola, vida extra).
- **Truco contra la compu:** 1 contra 1, sin flor, a 15. La CPU simula manos para decidir… **y miente**. Tiene nivel Normal y Difícil.
- **Tetris:** pozo completo de 10 × 16, sombra de caída y niveles.
- **Flappy**, **Viborita**, **2048**, **Buscaminas**, **Invasores** y **Simón**. En el Simón, el LED RGB de la placa se prende del color de cada flecha.

Cada juego tiene una demo animada en el menú, pausa y récord guardado. Mientras jugás, el WiFi se apaga solo para ahorrar batería.

## 📡 Pantalla WiFi

- Hora, calendario, clima, luz del día, luna e ISS.
- Spotify: play/pausa, temas y volumen.
- Acciones, cripto y monedas.
- Noticias.
- Monitor de PC e impresora Bambu.
- **Mensajes:** cualquiera en tu WiFi te escribe desde el celular y te llega a la consola.
- **Anotador de truco:** ← ellos, → nosotros.

Además:

- Todo se configura desde un **panel web** en el celular.
- Guarda **varias redes WiFi**.
- Se actualiza por **OTA** desde el navegador.
- Se **suspende**, y solo la despiertan las flechas laterales, así no se prende sola en el bolsillo.

## 🖨️ Impresión

- **Sin soportes.** El frente y la tapa van con la cara exterior en la cama; las teclas, con la pestaña abajo.
- PLA o PETG, capa de 0,2 mm, 3 perímetros, relleno del 15 al 20 %.
- **Imprimí primero las pruebas** (`prueba_botones_y_tactil`, `prueba_oled`, `prueba_cierre`, de 25 a 40 min cada una) para chequear el calce con tus componentes.
- **Versión bicolor (opcional):** `caja_frente_4botones_color` + `dibujo_capa1` como un solo objeto. Un solo cambio de filamento en la capa 1 dibuja el marco de la pantalla, "MINI·C3" y la cruz.
- Si las trabas de los pulsadores se te quiebran en PLA, probá en PETG.

## 🔧 Materiales

- 1× Waveshare **ESP32-C3-Zero**
- 1× OLED 1,3" 128×64 **SH1106** I²C
- 4× pulsador **12 × 12 × 6 mm**
- 1× módulo táctil **TTP223**
- 1× cargador **TP4056** (26 × 17 mm)
- 1× batería LiPo 3,7 V de hasta **40 × 30 × 8 mm**
- 1× interruptor **SS12D00**
- Cables finos, doble faz y goma espuma

## 🔌 Conexiones

| Qué | GPIO |
|---|---|
| OLED SDA / SCL | 8 / 9 |
| ↑ ↓ ← → (a GND) | 3 · 4 · 5 · 6 |
| Táctil TTP223 OUT | 7 |

Batería → TP4056 → interruptor → 5V del C3-Zero. **Apagá el interruptor antes de enchufar el USB-C para programar.**

## 🛠️ Armado

1. Grabá el firmware **antes de cerrar**.
   - Desde Chrome, en esptool-js, cargá `minic3_v1.12.0_factory.bin` en la dirección **0x0**. No hay que instalar nada.
2. Teclas → pulsadores (calzan con un *clic*) → táctil → OLED → interruptor.
3. Pegá el ESP32 y el cargador en la tapa con doble faz y cableá. Poné la batería con goma espuma.
4. Cerrá la tapa a presión.
5. Al prenderla, conectate a la red **MiniC3-Setup** (clave `minic3setup`), elegí tu WiFi y configurá todo en `http://minic3.local`.

La guía completa, con medidas y ajustes, está en el repo (`caja/LEEME.md` y `firmware/MiniC3/LEEME.md`).

## 🙏 Créditos

La base del sistema es el proyecto **Tinytosh: Mini Retro PC y Pantalla WiFi Inteligente ESP32**, de Uladzimir Hitsarau. ¡Gracias por compartirlo!
https://makerworld.com/es/models/2270326-tinytosh-mini-retro-pc-smart-wifi-display-esp32

Lo adaptamos a nuestro proyecto:

- un formato portátil con botones;
- pantalla SH1106;
- navegación con 5 controles;
- varias redes WiFi y OTA por navegador;
- bandeja de mensajes;
- todos los juegos y el anotador de truco;
- una caja diseñada desde cero.

---

## 7. Description (English)

---

**Mini C3** is a palm-sized handheld built with a **Waveshare ESP32-C3-Zero**, a **1.3" OLED**, a 4-button D-pad and a touch button. Size: **47.5 × 86.5 × 18.6 mm**.

It runs **9 games** and a **Truco score pad**. When you're not playing, it's a **smart WiFi display**: clock, weather, Spotify, messages, news and more.

🔗 **Firmware, source and binaries:** https://github.com/jorgefinelli90/minic3portable

## 🎮 Games

- **Arkanoid:** 6 levels and power-ups.
- **Truco vs CPU:** the Argentine card game, against a bluffing AI.
- **Tetris**, **Flappy**, **Snake**, **2048**, **Minesweeper**, **Invaders** and **Simon** (the board's RGB LED lights up in each arrow's color).

Every game has an animated demo, pause and saved high score. WiFi turns off while you play to save battery.

## 📡 Smart display

- Clock, calendar, weather, daylight, moon and ISS.
- Spotify controls.
- Stocks, crypto and currencies.
- News, PC monitor and Bambu printer status.
- Messages sent from any phone on your WiFi.

Also:

- **Web configuration panel**.
- **Multiple saved WiFi networks**.
- **OTA updates** from the browser.
- **Sleep mode** that only wakes with the side arrows.

*The UI is in Spanish.*

## 🖨️ Printing

- **No supports needed.** PLA or PETG, 0.2 mm layers, 3 walls, 15–20 % infill.
- **Print the test pieces first** (25–40 min each) to check the fit with your parts.
- **Optional two-color front:** a single filament change on layer 1.

## 🔧 Parts

- ESP32-C3-Zero
- 1.3" SH1106 I²C OLED
- 4× 12×12×6 mm tactile switches
- TTP223 touch module
- TP4056 charger (26×17 mm)
- 1S LiPo up to 40×30×8 mm
- SS12D00 slide switch

## 🔌 Wiring and assembly

**Wiring:**

- OLED SDA/SCL → GPIO 8/9.
- Buttons ↑ ↓ ← → → GPIO 3/4/5/6 (to GND).
- Touch → GPIO 7.
- Battery → TP4056 → switch → 5V pin.

**Flashing:** flash `minic3_v1.12.0_factory.bin` at **0x0** with esptool-js in Chrome, then join the **MiniC3-Setup** WiFi (password `minic3setup`).

Full guide in the GitHub repo.

## 🙏 Credits

The system is based on **Tinytosh: Mini Retro PC & Smart WiFi Display ESP32** by Uladzimir Hitsarau. Thank you!
https://makerworld.com/es/models/2270326-tinytosh-mini-retro-pc-smart-wifi-display-esp32

We adapted it into a portable, button-driven handheld:

- new display driver;
- games;
- messaging;
- multi-WiFi and OTA;
- a case designed from scratch.

---

## 8. Perfil de impresión (si subís el 3MF)

**Nombre del perfil:**

```
Frente + tapa + teclas · 0.2 mm · PLA · sin soportes
```

**Descripción del perfil:**

```
Placa completa: frente, tapa y las 4 teclas. Sin soportes. PLA 0.2 mm, 3 perímetros, relleno 15 %.
Antes de imprimir todo, conviene probar el calce con prueba_botones_y_tactil.stl y prueba_oled.stl.
```

**Ajustes:**

- Altura de capa: 0,2 mm.
- Paredes: 3.
- Relleno: 15 % gyroid.
- Soportes: no.
- Brim: no. Si la tapa se te levanta en las esquinas, usá un brim de 3 mm.
