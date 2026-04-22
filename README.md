# esp32pblBotak
## Projekt PBL - Pomiar Impedancji Metodą Trzech Napięć

Projekt służy do pomiaru impedancji metodą trzech napięć. Jest to technika pomiarowa pozwalająca na wyznaczenie wartości impedancji (`Zx`) oraz jej składowej rezystancyjnej (`Rx`) bezpośrednio z pomiarów napięć.

### Zasada działania

Układ pomiarowy składa się z trzech voltomierzy, które mierzą napięcia w trzech punktach obwodu:
- Napięcie na źródle zasilania
- Napięcie na elemencie referencyjnym (rezystor)
- Napięcie na mierzonej impedancji (`Zx`)

Na podstawie zmierzonych napięć, kod oblicza wartości `Zx` oraz `Rx` przy użyciu odpowiednich wzorów matematycznych.

### Wzory obliczeniowe

Po odczytaniu napięć z trzech voltomierzy, obliczenia wykonywane są według następujących zależności:

1. **Impedancja całkowita (Zx)** - wyznaczana z prądu płynącego w obwodzie
2. **Rezystancja (Rx)** - składowa rzeczywista impedancji

### Hardware

Projekt wykorzystuje mikrokontroler **ESP32 C3 Super Mini** do:
- Odczytywania napięć z 3 analogowych wejść pomiarowych
- Przetwarzania danych i obliczania wartości Zx i Rx
- Komunikacji wyników (np. przez UART, WiFi, lub Bluetooth)
