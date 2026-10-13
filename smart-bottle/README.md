# Smart Bottle

Sticlă inteligentă: STM32F429I-DISC1 citește nivelul apei și îl afișează pe LCD, un ESP32 îl trimite prin BLE,
iar aplicația web (Windows și Android, prin Web Bluetooth) afișează nivelul, istoricul și obiectivul zilnic.

```
Senzor ──► STM32F429I-DISC1 ──UART5──► ESP32 ──BLE (Nordic UART)──► Aplicație web (Chrome / Edge)
           ADC/HX711, filtru,           punte                       mL, consum, grafice, memento
           LCD, calibrare în RAM        transparentă                IndexedDB (istoric local)
```

Protocolul comun e descris în [PROTOCOL.md](PROTOCOL.md).

## Structură

| Folder | Conținut |
|---|---|
| `stm32/Core/Inc`, `stm32/Core/Src` | Modulele firmware (se adaugă într-un proiect STM32CubeIDE) |
| `esp32/SmartBottleBridge` | Sketch Arduino pentru puntea UART ↔ BLE |
| `web/` | Aplicația web (PWA), fără dependențe și fără build |
| `tests/` | Teste pe PC pentru filtru, protocol și logica aplicației |

## Cablaj

| De la | La | Observații |
|---|---|---|
| STM32 PC12 (UART5 TX) | ESP32 GPIO16 (RX2) | 3.3 V ↔ 3.3 V, fără divizor |
| STM32 PD2 (UART5 RX) | ESP32 GPIO17 (TX2) | |
| STM32 GND | ESP32 GND | masă comună, obligatoriu |
| STM32 PE4 | Senzor apă `+` | senzorul e alimentat doar în timpul citirii |
| STM32 PA5 (ADC1_IN5) | Senzor apă `S` | |
| STM32 GND | Senzor apă `−` | |
| *(varianta HX711)* STM32 PE2 / PE3 | HX711 DT / SCK | VCC 3.3 V; RATE la VCC pentru 80 SPS |

Pinii sunt aleși dintre cei nefolosiți de LCD, SDRAM și giroscop. **Verifică-i în UM1670 înainte de cablare.**
Dacă schimbi un pin, schimbă-l în `app_config.h` și în CubeMX.

## Firmware STM32 (STM32CubeIDE)

1. **Proiect nou** pentru placa *STM32F429I-DISC1*. La întrebarea „Initialize all peripherals with their default mode?” răspunde **Nu**, ca să nu se genereze cod care intră în conflict cu BSP-ul.
2. **Configurare în CubeMX:**
   - **RCC/Clock:** HSE 8 MHz, SYSCLK 168 sau 180 MHz. Întârzierile HX711 se adaptează singure prin `SystemCoreClock`.
   - **ADC1:** canal IN5 (PA5), rezoluție 12 biți, conversie simplă, fără DMA. Sampling time 84 cicluri sau mai mult (senzorul are impedanță mare).
   - **UART5:** asincron, 115200 8N1, cu **UART5 global interrupt** activat (NVIC).
   - **GPIO:** PE4 output push-pull, nivel inițial Low. Pentru HX711: PE3 output Low și PE2 input pull-up.
3. **Adaugă BSP-ul** din pachetul STM32CubeF4:
   - Fișiere: `Drivers/BSP/STM32F429I-Discovery/` (`stm32f429i_discovery.c`, `_lcd.c`, `_sdram.c`), `Drivers/BSP/Components/ili9341/` și `Utilities/Fonts/`.
   - Adaugă căile în *Include paths*.
   - În `stm32f4xx_hal_conf.h` activează `HAL_LTDC_MODULE_ENABLED`, `HAL_DMA2D_MODULE_ENABLED`, `HAL_SDRAM_MODULE_ENABLED`, `HAL_SPI_MODULE_ENABLED` și `HAL_I2C_MODULE_ENABLED`.
4. **Copiază** `stm32/Core/Inc/*` și `stm32/Core/Src/*` în proiect.
5. **În `main.c`:**
   ```c
   /* USER CODE BEGIN Includes */
   #include "app.h"
   /* USER CODE BEGIN 2 */
   app_init();
   /* în while (1), USER CODE BEGIN 3 */
   app_loop();
   ```
   Callback-urile `HAL_UART_RxCpltCallback` și `HAL_UART_ErrorCallback` sunt deja în `link.c`. Nu le defini și în altă parte.
6. **Varianta cu celulă de sarcină:** pune `#define SENSOR_USE_HX711 1` în `app_config.h`.

**LED-uri:** LD3 (verde) e aprins când telefonul e conectat, iar LD4 (roșu) clipește la eroare de senzor.

## ESP32

Deschide `esp32/SmartBottleBridge/SmartBottleBridge.ino` în Arduino IDE, alege placa **ESP32 Dev Module** și încarcă sketch-ul. Funcționează cu ambele versiuni de core ESP32 (2.x și 3.x).

**Test fără aplicație:** deschide nRF Connect pe telefon, conectează-te la `SmartBottle` și activează notificările pe caracteristica TX. Ar trebui să vezi `L,...` în fiecare secundă.

## Aplicația web

**Local (Windows):**
```
cd web
python -m http.server 8000
```
Deschide `http://localhost:8000` în Chrome sau Edge. Pe `localhost` Web Bluetooth funcționează și fără HTTPS.

**Android:** pagina trebuie servită prin HTTPS. Ai două variante:
- Urci folderul `web/` pe GitHub Pages, Cloudflare Pages sau Netlify (sunt doar fișiere statice).
- Folosești `adb reverse tcp:8000 tcp:8000` și deschizi `http://localhost:8000` în Chrome pe telefon.

Din meniul Chrome poți alege **Instalează aplicația** (PWA).

**Butonul Demo** simulează dispozitivul cu înghițituri și reumpleri, deci poți testa interfața fără hardware.

**Utilizare:**
1. Conectează-te la dispozitiv.
2. Pune recipientul gol, așteaptă starea „stabil” și apasă **Setează GOL**.
3. Umple recipientul, așteaptă din nou și apasă **Setează PLIN**.
4. Alege forma recipientului și capacitatea.

Calibrarea se salvează în browser și se retrimite automat la fiecare conectare.

**Limitări cunoscute:**
- Nu funcționează în Firefox sau pe iOS.
- Memento-ul de inactivitate apare doar cât timp pagina e deschisă (fără activitate în fundal).

## Teste

```
cd tests
gcc -Wall -Wextra -I../stm32/Core/Inc test_host.c ../stm32/Core/Src/filter.c ../stm32/Core/Src/protocol.c -o t && ./t
node test_logic.mjs
```
