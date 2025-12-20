# Waveshare ESP32-S3 Touch LCD 7B — Pinmap validé

| Fonction                | GPIO / EXIO | Détails |
|-------------------------|-------------|---------|
| I2C SDA                 | GPIO8       | Bus partagé (GT911 + CH32V003) |
| I2C SCL                 | GPIO9       | Bus partagé (mutex unique) |
| Touch IRQ               | GPIO4       | Front descendant, ISR minimale |
| Touch RESET             | EXIO1       | Piloté via CH32V003 |
| SD SPI MOSI             | GPIO11      | SDSPI hôte SPI2 |
| SD SPI MISO             | GPIO13      | SDSPI hôte SPI2 |
| SD SPI SCLK             | GPIO12      | SDSPI hôte SPI2 |
| SD CS (réel)            | EXIO4       | Actif bas via CH32V003 |
| SD CS (dummy)           | GPIO6       | Sélection factice pour SDSPI (Option A) |
| LCD VSYNC               | GPIO3       | RGB |
| LCD HSYNC               | GPIO46      | RGB |
| LCD DE                  | GPIO5       | RGB |
| LCD PCLK                | GPIO7       | RGB |
| LCD Data0 (G3)          | GPIO0       | |
| LCD Data1 (R3)          | GPIO1       | |
| LCD Data2 (R4)          | GPIO2       | |
| LCD Data3 (B7)          | GPIO10      | |
| LCD Data4 (B3)          | GPIO14      | |
| LCD Data5 (B6)          | GPIO17      | |
| LCD Data6 (B5)          | GPIO18      | |
| LCD Data7 (G7)          | GPIO21      | |
| LCD Data8 (B4)          | GPIO38      | |
| LCD Data9 (G2)          | GPIO39      | |
| LCD Data10 (R7)         | GPIO40      | |
| LCD Data11 (R6)         | GPIO41      | |
| LCD Data12 (R5)         | GPIO42      | |
| LCD Data13 (G4)         | GPIO45      | |
| LCD Data14 (G6)         | GPIO47      | |
| LCD Data15 (G5)         | GPIO48      | |
| LCD VDD / VCOM enable   | EXIO6       | 1 = ON |
| LCD Backlight enable    | EXIO2       | 1 = ON |

IO expander : CH32V003 @0x24 sur le bus I²C partagé.
