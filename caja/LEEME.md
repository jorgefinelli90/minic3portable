# Mini C3: caja imprimible

La caja mide **47,5 × 86,5 × 18,6 mm**. Lleva:

- placa Waveshare **ESP32-C3-Zero**;
- OLED 1,3" **SH1106** I²C;
- 4 pulsadores de **12 × 12 × 6 mm**;
- táctil **TTP223**;
- cargador **TP4056** de 26 × 17 mm;
- batería LiPo 1S;
- interruptor SS12D00.

Todo se imprime **sin soportes**. Los pulsadores, la pantalla y el táctil calzan a presión, y la tapa cierra con trabas, sin tornillos.

## Archivos (`stl/`)

| Archivo | Qué es | ¿Hace falta? |
|---|---|---|
| `caja_frente_4botones.stl` | Frente: pantalla, cruz de 4 botones y táctil | Sí |
| `tapa.stl` | Tapa trasera a presión (5 trabas) | Sí |
| `teclas_x4.stl` | Las 4 teclas con flecha | Sí, estas o las de encastre |
| `teclas_x4_encastre.stl` | Variante de las teclas con encastre cuadrado de 4,2 × 4,2 abajo, para pulsadores con vástago cuadrado | Opcional |
| `caja_frente_4botones_color.stl` + `dibujo_capa1.stl` | El mismo frente con un dibujo en la primera capa en otro color: marco de la pantalla, "MINI·C3", la cruz y el disco del botón A | Opcional, reemplaza al frente |
| `prueba_botones_y_tactil.stl` | Solo la parte de los botones y el táctil (~40 min), para probar el calce antes de imprimir todo | Recomendado |
| `prueba_oled.stl` | Solo el marco de la OLED (~25 min) | Recomendado |
| `prueba_cierre.stl` | Los últimos 5 mm de la caja, para probar el cierre con la tapa (~25 min) | Recomendado |

`mini_handheld.py` e `inlay.py` son los modelos paramétricos de los que salen los STL:

1. Instalá lo necesario: `pip install manifold3d trimesh numpy matplotlib`.
2. Corré `python3 mini_handheld.py` para regenerar los STL. Con `--check` mide interferencias.
3. Las medidas de la caja se calculan a partir de las piezas: si cambiás un parámetro, todo se reacomoda.

> En el laminador ves la caja desde adentro (el frente va apoyado en la cama), por eso el táctil aparece a la izquierda. Dada vuelta queda a la derecha.

## Impresión

- PLA o PETG, capa de 0,2 mm, 3 perímetros, relleno del 15 al 20 %.
- El frente y la tapa van con la cara exterior en la cama. Las teclas van con la pestaña abajo.
- Si el pulsador entra muy justo o muy flojo, cambiá `SW_CLR` en el `.py` (holgura por lado, hoy 0,15 mm). Si las teclas rozan, cambiá `KEY_CLR` (hoy 0,25).
- Las trabas de los pulsadores trabajan cerca del límite del PLA. Si alguna se quiebra, bajá `LIP` a 0,55 o imprimí el frente en PETG, que es más flexible.

### Dibujo de primera capa (opcional, cambio de color)

1. Arrastrá juntos `caja_frente_4botones_color.stl` y `dibujo_capa1.stl` y respondé **Sí** a "¿cargar como un solo objeto con varias partes?".
2. Asigná el filamento 1 al dibujo y el 2 a la carcasa.
3. Desactivá la torre de purga. En la vista previa tiene que haber un solo cambio de filamento, en la capa 1.
4. Con AMS se hace solo. Sin AMS, en la pausa cambiá el filamento, extruí hasta que salga limpio y reanudá.

Si el dibujo es más claro que la carcasa, con una sola capa puede transparentar. En ese caso usá `INLAY_T = 0.4` en `inlay.py` para hacerlo de 2 capas.

## Armado

1. **Grabá el firmware antes de cerrar** (ver `firmware/`).
2. **Teclas:** metelas desde adentro por el túnel, con la flecha hacia afuera. La pestaña apoya contra el frente.
3. **Pulsadores 12 × 12 × 6:** empujalos por el mismo túnel, con el vástago hacia el frente, hasta que hagan *clic*.
   - Las patas van hacia las paredes bajas del bolsillo.
   - El botón de **abajo** va girado 90°.
   - Para el cableado usá dos patas en diagonal, cortadas a ~2 mm.
4. **Táctil TTP223:** va parado en su bolsillo, con la cara del círculo contra el frente. Fijalo con doble faz.
5. **OLED:** entra desde atrás. La cinta flex va arriba y los pines abajo; se ubica sola con 4 pines.
6. **Interruptor SS12D00:** va en el lateral derecho, con una gota de pegamento.
7. **ESP32-C3-Zero y TP4056:** van con doble faz en los bolsillos de la tapa, con los componentes hacia el frente y los conectores hacia la base.
8. **Batería:** va detrás de la OLED; entra hasta 40 × 30 × 8 mm. Ponele una goma espuma.
9. **Tapa:** apoyala derecha y apretá hasta el clic. Para abrirla, metés una uña o una moneda en la ranurita de la base, entre los dos puertos.

## Conexiones

| Qué | Pin del ESP32-C3-Zero |
|---|---|
| OLED SDA / SCL | GPIO 8 / 9 |
| OLED VCC / GND | 3V3 / GND |
| Botones ↑ ↓ ← → (cada uno al GND) | GPIO 3 · 4 · 5 · 6 |
| TTP223 OUT (VCC a 3V3, GND a GND) | GPIO 7 |
| Nivel de batería (divisor 100k/100k, opcional) | GPIO 0 |
| LED RGB de la placa | GPIO 10 (ya viene en la placa) |

**Energía:**

- Batería → TP4056 (B+ / B−).
- OUT+ → interruptor → pin 5V del C3-Zero.
- OUT− → GND.

**Apagá el interruptor antes de enchufar el USB-C de la placa para programarla**, o poné un diodo Schottky (1N5817) en serie.
