# CM 3 CH PRO — VST3 (Windows 10/11)

Compresor multibanda de 3 canales (LOW / MID / HIGH) con crossover Linkwitz-Riley, saturación por banda,
LVC (soft clipper), mute/limit/x10 por banda, entrada/salida y bypass. UI = `ui/index.html` dentro de WebView2.

## Compilar en GitHub
1. Crea un repo vacío, sube TODO el contenido de esta carpeta (incluida `.github/`).
2. `git add . && git commit -m "init" && git push`
3. Pestaña **Actions → Build VST3 (Windows) → artifact `CM3CH-PRO-VST3-Windows`**.
4. Copia la carpeta `CM 3 CH PRO.vst3` a `C:\Program Files\Common Files\VST3\` y re-escanea en tu DAW.

Requiere WebView2 Runtime (ya viene en Windows 11 y en Windows 10 actualizado).

## Mapeo FST → plugin
| FST (Patcher) | Plugin |
|---|---|
| 3× Fruity Multiband Compressor (BAJOS/MEDIOS/ALTOS) | compresor por banda: TH, RATIO, GAIN, ATT, REL, KNEE, LIMIT |
| Crossovers LOW FREQ / HI FREQ | `lo` 20–500 Hz, `hi` 2–20 kHz (LR4) |
| 3× Fruity Blood Overdrive + SAT/x10 | `sat`, `sam`, `x10` (tanh, aproximación) |
| Fruity Soft Clipper "LVC" | `lvc` (codo −6 dB, techo 0 dBFS) |
| Balances MUTELOW/MIDS/HI, BYPASS | `mu`, `byp` (con rampa anti-click) |
| Input / Output | `inn`, `out` (±24 dB) |

Limitaciones: `FIR` se guarda pero el DSP es IIR; Blood Overdrive y los compresores de FL son aproximaciones, no clones exactos.
