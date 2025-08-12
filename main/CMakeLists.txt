idf_component_register(SRCS "telemetry_project_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi nvs_flash esp_http_server cjson)