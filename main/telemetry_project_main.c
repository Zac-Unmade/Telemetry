#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"

#define LED_PIN GPIO_NUM_4  
#define LED_ALWAYS_ON GPIO_NUM_2

#define MAX_ENTRIES 100  

#define GET_INT(item)   ((item) && cJSON_IsNumber(item) ? (item)->valueint : ((item) && cJSON_IsString(item) ? atoi(cJSON_GetStringValue(item)) : 0))
#define GET_FLOAT(item) ((item) && cJSON_IsNumber(item) ? (float)(item)->valuedouble : ((item) && cJSON_IsString(item) ? atof(cJSON_GetStringValue(item)) : 0.0f))

typedef struct {
    int speed; 
    int torque;
    int rpm;
    int coolant_temp;
    float battery_voltage;
    float alternator_voltage;
    float current_draw;
    int soc; // state of charge
} TelemetryData;

TelemetryData telemetry_log[MAX_ENTRIES];
int telemetry_count = 0;

#define WIFI_SSID "Your_SSID"
#define WIFI_PASS "enter password here"

static const char *TAG = "web_server";

esp_err_t root_get_handler(httpd_req_t *req) {
    const char* html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>ESP32 Telemetry</title>\n"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "<style>\n"
        "  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }\n"
        "  .container { max-width: 900px; margin: 0 auto; background: #fff; padding: 25px 30px; border-radius: 10px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }\n"
        "  h1 { text-align: center; color: #333; margin-bottom: 25px; }\n"
        "  .form-row { display: flex; gap: 25px; align-items: flex-start; flex-wrap: wrap; }\n"
        "  .col { flex: 1; min-width: 260px; }\n"
        "  .inputs { margin-top: 8px; }\n"
        "  .inputs input { display: block; margin-bottom: 10px; width: 95%; padding: 8px 10px; border: 1px solid #ccc; border-radius: 5px; font-size: 14px; transition: border-color 0.3s ease; }\n"
        "  .inputs input:focus { border-color: #0078d4; outline: none; }\n"
        "  input[type=submit] { display: block; margin: 20px auto 30px auto; padding: 10px 25px; font-size: 16px; background: #0078d4; color: #fff; border: none; border-radius: 6px; cursor: pointer; transition: background-color 0.3s ease; }\n"
        "  input[type=submit]:hover { background: #005a9e; }\n"
        "  h2, h3 { color: #222; }\n"
        "  canvas { margin-top: 10px; border-radius: 8px; background: linear-gradient(135deg, #e0e7ff, #f9fafb); box-shadow: 0 2px 6px rgba(0,0,0,0.05); }\n"
        "  .metrics { margin-top: 30px; padding: 20px; border-radius: 8px; background: #f9f9fb; box-shadow: inset 0 0 5px #ddd; }\n"
        "  .metric-item { margin-bottom: 12px; font-size: 16px; color: #444; }\n"
        "  .status { font-weight: 600; padding: 4px 10px; border-radius: 6px; }\n"
        "  .ok { background-color: #c8e6c9; color: #256029; }\n"
        "  .warn { background-color: #ffcdd2; color: #b71c1c; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<div class='container'>\n"
        "<h1>Telemetry Data</h1>\n"
        "<form id='telemetryForm'>\n"
        "<div class='form-row'>\n"
        "  <div class='col'><h2>Engine / Drivetrain</h2></div>\n"
        "  <div class='col'><h2>Electrical System</h2></div>\n"
        "</div>\n"
        "<div class='form-row'>\n"
        "  <div class='col inputs'>\n"
        "    Speed: <input type='number' id='speed'>\n"
        "    Torque: <input type='number' id='torque'>\n"
        "    RPM: <input type='number' id='rpm'>\n"
        "    Coolant Temp (°C): <input type='number' id='coolant_temp'>\n"
        "  </div>\n"
        "  <div class='col inputs'>\n"
        "    Battery Voltage (V): <input type='number' step='0.01' id='battery_voltage'>\n"
        "    Alternator Voltage (V): <input type='number' step='0.01' id='alternator_voltage'>\n"
        "    Current Draw (A): <input type='number' step='0.01' id='current_draw'>\n"
        "    SoC (%): <input type='number' id='soc'>\n"
        "  </div>\n"
        "</div>\n"
        "<input type='submit' value='Submit'>\n"
        "<div class='form-row'>\n"
        "  <div class='col'>\n"
        "    <h3>Engine / Drivetrain Graph</h3>\n"
        "    <canvas id='engineChart' width='400' height='200'></canvas>\n"
        "  </div>\n"
        "  <div class='col'>\n"
        "    <h3>Electrical System Graph</h3>\n"
        "    <canvas id='electricalChart' width='400' height='200'></canvas>\n"
        "  </div>\n"
        "</div>\n"
        "</form>\n"
        "<div class='metrics'>\n"
        "  <h2>Live Metrics</h2>\n"
        "  <div class='metric-item'>Current Power: <span id='currentPower'>-</span> kW</div>\n"
        "  <div class='metric-item'>Average Speed: <span id='averageSpeed'>-</span> km/h</div>\n"
        "  <div class='metric-item'>Predicted Battery Depletion Time: <span id='batteryTime'>-</span> hrs</div>\n"
        "  <div class='metric-item'>Overheat Status: <span id='overheatStatus' class='status ok'>OK</span></div>\n"
        "  <div class='metric-item'>Battery Status: <span id='batteryStatus' class='status ok'>OK</span></div>\n"
        "</div>\n"
        "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>\n"
        "<script>\n"
        "const form = document.getElementById('telemetryForm');\n"
        "const ctxEngine = document.getElementById('engineChart').getContext('2d');\n"
        "const ctxElectrical = document.getElementById('electricalChart').getContext('2d');\n"
        "\n"
        "let speedData = [];\n"
        "\n"
        "let engineChart = new Chart(ctxEngine, {\n"
        "    type: 'line',\n"
        "    data: {\n"
        "        labels: [],\n"
        "        datasets: [\n"
        "            { label: 'Speed', data: [], borderColor: 'red', fill: false },\n"
        "            { label: 'Torque', data: [], borderColor: 'blue', fill: false },\n"
        "            { label: 'Coolant Temp', data: [], borderColor: 'orange', fill: false }\n"
        "        ]\n"
        "    },\n"
        "    options: {\n"
        "        plugins: {\n"
        "            legend: { labels: { font: { size: 14 }, color: '#222' } }\n"
        "        },\n"
        "        scales: {\n"
        "            x: { ticks: { color: '#444' } },\n"
        "            y: { ticks: { color: '#444' }, beginAtZero: true }\n"
        "        }\n"
        "    }\n"
        "});\n"
        "\n"
        "let electricalChart = new Chart(ctxElectrical, {\n"
        "    type: 'line',\n"
        "    data: {\n"
        "        labels: [],\n"
        "        datasets: [\n"
        "            { label: 'Battery Voltage', data: [], borderColor: 'purple', fill: false },\n"
        "            { label: 'Alternator Voltage', data: [], borderColor: 'pink', fill: false },\n"
        "            { label: 'Current Draw', data: [], borderColor: 'brown', fill: false }\n"
        "        ]\n"
        "    },\n"
        "    options: {\n"
        "        plugins: {\n"
        "            legend: { labels: { font: { size: 14 }, color: '#222' } }\n"
        "        },\n"
        "        scales: {\n"
        "            x: { ticks: { color: '#444' } },\n"
        "            y: { ticks: { color: '#444' }, beginAtZero: true }\n"
        "        }\n"
        "    }\n"
        "});\n"
        "\n"
        "form.addEventListener('submit', async (e) => {\n"
        "    e.preventDefault();\n"
        "    const payload = {\n"
        "        speed: Number(document.getElementById('speed').value),\n"
        "        torque: Number(document.getElementById('torque').value),\n"
        "        rpm: Number(document.getElementById('rpm').value),\n"
        "        coolant_temp: Number(document.getElementById('coolant_temp').value),\n"
        "        battery_voltage: parseFloat(document.getElementById('battery_voltage').value),\n"
        "        alternator_voltage: parseFloat(document.getElementById('alternator_voltage').value),\n"
        "        current_draw: parseFloat(document.getElementById('current_draw').value),\n"
        "        soc: Number(document.getElementById('soc').value)\n"
        "    };\n"
        "    await fetch('/submit', {\n"
        "        method: 'POST',\n"
        "        headers: {'Content-Type': 'application/json'},\n"
        "        body: JSON.stringify(payload)\n"
        "    });\n"
        "    loadData();\n"
        "});\n"
        "\n"
        "async function loadData() {\n"
        "    const res = await fetch('/data');\n"
        "    const data = await res.json();\n"
        "    const labels = data.map((_, i) => i + 1);\n"
        "    engineChart.data.labels = labels;\n"
        "    electricalChart.data.labels = labels;\n"
        "    engineChart.data.datasets[0].data = data.map(e => e.speed);\n"
        "    engineChart.data.datasets[1].data = data.map(e => e.torque);\n"
        "    engineChart.data.datasets[2].data = data.map(e => e.coolant_temp);\n"
        "    electricalChart.data.datasets[0].data = data.map(e => e.battery_voltage);\n"
        "    electricalChart.data.datasets[1].data = data.map(e => e.alternator_voltage);\n"
        "    electricalChart.data.datasets[2].data = data.map(e => e.current_draw);\n"
        "    engineChart.update();\n"
        "    electricalChart.update();\n"
        "\n"
        "    if (data.length) {\n"
        "      const latest = data[data.length - 1];\n"
        "      const power = ((latest.speed * latest.torque) / 9549).toFixed(2);  // Simplified power calc\n"
        "      document.getElementById('currentPower').textContent = power;\n"
        "\n"
        "      const avgSpeed = (data.reduce((acc, val) => acc + val.speed, 0) / data.length).toFixed(1);\n"
        "      document.getElementById('averageSpeed').textContent = avgSpeed;\n"
        "\n"
        "      const batteryTime = latest.soc > 0 && latest.current_draw > 0 ? (latest.soc * latest.battery_voltage / latest.current_draw).toFixed(2) : '-';\n"
        "      document.getElementById('batteryTime').textContent = batteryTime;\n"
        "\n"
        "      const overheatEl = document.getElementById('overheatStatus');\n"
        "      if (latest.coolant_temp > 90) {\n"
        "        overheatEl.textContent = 'WARNING';\n"
        "        overheatEl.className = 'status warn';\n"
        "      } else {\n"
        "        overheatEl.textContent = 'OK';\n"
        "        overheatEl.className = 'status ok';\n"
        "      }\n"
        "\n"
        "      const batteryEl = document.getElementById('batteryStatus');\n"
        "      if (latest.soc < 20) {\n"
        "        batteryEl.textContent = 'LOW';\n"
        "        batteryEl.className = 'status warn';\n"
        "      } else {\n"
        "        batteryEl.textContent = 'OK';\n"
        "        batteryEl.className = 'status ok';\n"
        "      }\n"
        "    }\n"
        "}\n"
        "\n"
        "setInterval(loadData, 3000);\n"
        "loadData();\n"
        "</script>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t submit_post_handler(httpd_req_t *req) {
    char content[256];
    int total_len = req->content_len;
    int received = 0;

    if (total_len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    while (received < total_len) {
        int ret = httpd_req_recv(req, content + received, total_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
            return ESP_FAIL;
        }
        received += ret;
    }

    content[received] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *speed_item = cJSON_GetObjectItem(json, "speed");
    cJSON *torque_item = cJSON_GetObjectItem(json, "torque");
    cJSON *rpm_item = cJSON_GetObjectItem(json, "rpm");
    cJSON *coolant_temp_item = cJSON_GetObjectItem(json, "coolant_temp");
    cJSON *battery_voltage_item = cJSON_GetObjectItem(json, "battery_voltage");
    cJSON *alternator_voltage_item = cJSON_GetObjectItem(json, "alternator_voltage");
    cJSON *current_draw_item = cJSON_GetObjectItem(json, "current_draw");
    cJSON *soc_item = cJSON_GetObjectItem(json, "soc");

    TelemetryData entry = {
        .speed = GET_INT(speed_item),
        .torque = GET_INT(torque_item),
        .rpm = GET_INT(rpm_item),
        .coolant_temp = GET_INT(coolant_temp_item),
        .battery_voltage = GET_FLOAT(battery_voltage_item),
        .alternator_voltage = GET_FLOAT(alternator_voltage_item),
        .current_draw = GET_FLOAT(current_draw_item),
        .soc = GET_INT(soc_item)
    };

    ESP_LOGI(TAG, "Speed=%d Torque=%d RPM=%d Coolant=%d Batt=%.2f Alt=%.2f Curr=%.2f SoC=%d",
         entry.speed, entry.torque, entry.rpm, entry.coolant_temp,
         entry.battery_voltage, entry.alternator_voltage, entry.current_draw, entry.soc);
    
    if (telemetry_count < MAX_ENTRIES) {
        telemetry_log[telemetry_count++] = entry;
    }

    gpio_set_level(LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LED_PIN, 0);

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t data_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < telemetry_count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "speed", telemetry_log[i].speed);
        cJSON_AddNumberToObject(entry, "torque", telemetry_log[i].torque);
        cJSON_AddNumberToObject(entry, "rpm", telemetry_log[i].rpm);
        cJSON_AddNumberToObject(entry, "coolant_temp", telemetry_log[i].coolant_temp);
        cJSON_AddNumberToObject(entry, "battery_voltage", telemetry_log[i].battery_voltage);
        cJSON_AddNumberToObject(entry, "alternator_voltage", telemetry_log[i].alternator_voltage);
        cJSON_AddNumberToObject(entry, "current_draw", telemetry_log[i].current_draw);
        cJSON_AddNumberToObject(entry, "soc", telemetry_log[i].soc);
        cJSON_AddItemToArray(root, entry);
    }

    char *json_str = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    cJSON_Delete(root);
    free(json_str);

    return ESP_OK;
}

httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};
httpd_uri_t submit_uri = {
    .uri       = "/submit",
    .method    = HTTP_POST,
    .handler   = submit_post_handler,
    .user_ctx  = NULL
};
httpd_uri_t data_uri = {
    .uri       = "/data",
    .method    = HTTP_GET,
    .handler   = data_get_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &submit_uri); 
        httpd_register_uri_handler(server, &data_uri); 
    }
    return server;
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP. Starting web server...");
        start_webserver();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Reconnecting...");
        esp_wifi_connect();
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi STA initialized.");
}

void app_main(void) {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LED_ALWAYS_ON);
    gpio_set_direction(LED_ALWAYS_ON, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_ALWAYS_ON, 1);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_init_sta();
}

