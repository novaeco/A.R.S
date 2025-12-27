# Monitor : désactiver le décodage d’adresses

Des traces du type `--- 0x40000000: _WindowOverflow4` peuvent apparaître parce que le monitor décode des valeurs (ex. arguments CMD41 SD) comme des adresses. Pour supprimer ce bruit :

- Ligne de commande : `idf.py -p <PORT> monitor --disable-address-decoding`
- Ou variable d’environnement : `ESP_MONITOR_DECODE=0`

Référence : [ESP-IDF monitor](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-monitor.html).
