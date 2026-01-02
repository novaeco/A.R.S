# Waveshare ESP32-S3 Touch LCD 7B — Pinmap validé

## Connexions I2C et SD

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

## Connexions LCD RGB (signaux de contrôle)

| Fonction                | GPIO        | Détails |
|-------------------------|-------------|---------|
| LCD VSYNC               | GPIO3       | RGB sync vertical |
| LCD HSYNC               | GPIO46      | RGB sync horizontal |
| LCD DE                  | GPIO5       | Data Enable |
| LCD PCLK                | GPIO7       | Pixel Clock |

## Câblage physique LCD RGB (GPIO → Panel)

Le panel LCD utilise un câblage **non-standard** où les bits de couleur sont
dispersés sur différentes lignes GPIO. La table ci-dessous montre quelle GPIO
est câblée à quel bit couleur du panel LCD.

| GPIO        | Pin Panel | Canal   |
|-------------|-----------|---------|
| GPIO0       | G3        | Green   |
| GPIO1       | R3        | Red     |
| GPIO2       | R4        | Red     |
| GPIO10      | B7        | Blue    |
| GPIO14      | B3        | Blue    |
| GPIO17      | B6        | Blue    |
| GPIO18      | B5        | Blue    |
| GPIO21      | G7        | Green   |
| GPIO38      | B4        | Blue    |
| GPIO39      | G2        | Green   |
| GPIO40      | R7        | Red     |
| GPIO41      | R6        | Red     |
| GPIO42      | R5        | Red     |
| GPIO45      | G4        | Green   |
| GPIO47      | G6        | Green   |
| GPIO48      | G5        | Green   |

## Mapping RGB565 pour esp_lcd_panel_rgb

Le driver `esp_lcd_panel_rgb` utilise le format RGB565 où les 16 bits d'un
pixel sont organisés comme suit :
- Bits 0-4 (DATA0-DATA4) : Blue B3-B7
- Bits 5-10 (DATA5-DATA10) : Green G2-G7  
- Bits 11-15 (DATA11-DATA15) : Red R3-R7

Pour envoyer les couleurs correctement, on doit **réarranger** les GPIO dans
`data_gpio_nums[]` selon le canal couleur, pas selon le numéro de ligne physique.

**Mapping corrigé dans board.h :**
```c
// Blue (DATA0-4) -> GPIO vers pins B3-B7 du panel
DATA0  = GPIO14 (B3)
DATA1  = GPIO38 (B4)
DATA2  = GPIO18 (B5)
DATA3  = GPIO17 (B6)
DATA4  = GPIO10 (B7)

// Green (DATA5-10) -> GPIO vers pins G2-G7 du panel
DATA5  = GPIO39 (G2)
DATA6  = GPIO0  (G3)
DATA7  = GPIO45 (G4)
DATA8  = GPIO48 (G5)
DATA9  = GPIO47 (G6)
DATA10 = GPIO21 (G7)

// Red (DATA11-15) -> GPIO vers pins R3-R7 du panel
DATA11 = GPIO1  (R3)
DATA12 = GPIO2  (R4)
DATA13 = GPIO42 (R5)
DATA14 = GPIO41 (R6)
DATA15 = GPIO40 (R7)
```

## IO Expander

| Fonction                | EXIO        | Détails |
|-------------------------|-------------|---------|
| Touch RESET             | EXIO1       | Actif bas |
| LCD Backlight enable    | EXIO2       | 1 = ON |
| LCD RESET               | EXIO3       | Actif bas |
| SD CS                   | EXIO4       | Actif bas |
| USB/CAN Select          | EXIO5       | 0=USB, 1=CAN |
| LCD VDD / VCOM enable   | EXIO6       | 1 = ON |

IO expander : CH32V003 @0x24 sur le bus I²C partagé.
