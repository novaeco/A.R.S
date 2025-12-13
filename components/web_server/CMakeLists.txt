idf_component_register(SRCS "src/web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server core_service reptile_storage espressif__cjson board net sd
                       EMBED_FILES src/www/index.html src/www/app.css src/www/app.js)