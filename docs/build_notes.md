# Notes de build ESP-IDF

- Avertissement CMake « Git submodule components/esp_wifi/lib is out of date » observé sur esp-idf master : exécuter `git submodule update --init --recursive` dans `$IDF_PATH` avant de relancer `idf.py`. Ceci remet le sous-module wifi à jour sans modifier le projet applicatif.
