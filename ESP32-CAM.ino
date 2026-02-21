#include "esp_camera.h"   // Библиотека для работы с камерой ESP32
#include "FS.h"           // Файловая система
#include "SD_MMC.h"       // Работа с SD-картой через SD_MMC
#include <WiFi.h>         // Подключение к WiFi
#include <WebServer.h>    // HTTP-сервер для управления через веб

// ======================= НАСТРОЙКИ WiFi =======================
const char* ssid = "**********";        // Имя вашей WiFi сети
const char* password = "*********"; // Пароль WiFi

WebServer server(80); // Создаём веб-сервер на порту 80

// ======================= ИНИЦИАЛИЗАЦИЯ КАМЕРЫ =======================
// Стандартные настройки для камеры AI-Thinker
void initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // Пины подключения камеры к ESP32
    config.pin_d0 = 5;
    config.pin_d1 = 18;
    config.pin_d2 = 19;
    config.pin_d3 = 21;
    config.pin_d4 = 36;
    config.pin_d5 = 39;
    config.pin_d6 = 34;
    config.pin_d7 = 35;
    config.pin_xclk = 0;
    config.pin_pclk = 22;
    config.pin_vsync = 25;
    config.pin_href = 23;
    config.pin_sscb_sda = 26;
    config.pin_sscb_scl = 27;
    config.pin_pwdn = 32;  // Управление питанием камеры
    config.pin_reset = -1;  // Без использования сброса
    config.xclk_freq_hz = 20000000; // Частота XCLK
    config.pixel_format = PIXFORMAT_JPEG; // Формат изображения JPEG
    config.frame_size = FRAMESIZE_SVGA;   // Размер кадра
    config.jpeg_quality = 12;             // Качество JPEG (0-63, меньше — лучше)
    config.fb_count = 2;                  // Количество буферов кадра

    // Инициализация камеры
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Ошибка инициализации камеры: 0x%x\n", err);
        while(true) delay(1000); // Если камера не инициализировалась — остановка
    }
}

// ======================= ФУНКЦИИ РАБОТЫ С SD-КАРТОЙ =======================

// Создаём HTML страницу со списком фотографий на SD карте
String listPhotosHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>ESP32-CAM Фотографии</title>";
    html += "<style>body{font-family:Arial; background:#f2f2f2; text-align:center;} ul{list-style:none;} a{color:#007BFF; text-decoration:none;} a:hover{color:#0056b3;}</style>";
    html += "</head><body>";
    html += "<h1>Фотографии на SD карте</h1><ul>";

    File root = SD_MMC.open("/"); // Открываем корневой каталог
    if(!root){
        html += "<li>Ошибка открытия каталога</li>";
    } else {
        File file = root.openNextFile();
        bool found = false;
        while(file){
            String name = file.name();
            if(!file.isDirectory() && name.endsWith(".jpg")) {
                found = true;
                html += "<li><a href=\"" + name + "\" target=\"_blank\">" + name + "</a></li>";
            }
            file = root.openNextFile();
        }
        if(!found){
            html += "<li>Фото не найдены</li>";
        }
        root.close();
    }

    html += "</ul>";
    html += "<p><a href=\"/\">Вернуться на главную</a></p>";
    html += "</body></html>";
    return html;
}

// ======================= ОБРАБОТЧИКИ HTTP =======================

// Главная страница с кнопками управления
void handleRoot() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <title>ESP32-CAM</title>
        <style>
            body{font-family:Arial; text-align:center; background:#f0f0f0; padding:20px;}
            button{padding:10px 20px; font-size:18px; cursor:pointer; margin:10px;}
            #msg{color:green; font-weight:bold;}
            a{display:block; margin:10px; color:#007BFF; text-decoration:none;}
            a:hover{color:#0056b3;}
        </style>
        <script>
            function takePhoto() {
                var msgElem = document.getElementById('msg');
                msgElem.textContent = 'Делается фото...';
                fetch('/capture')
                    .then(response => response.text())
                    .then(text => { msgElem.textContent = text; })
                    .catch(err => { msgElem.textContent = 'Ошибка: ' + err; });
            }
        </script>
    </head>
    <body>
        <h1>ESP32-CAM</h1>
        <button onclick="takePhoto()">Сделать фото</button>
        <p id="msg"></p>
        <a href="/photos">Просмотр фотографий</a>
        <a href="/stream">Потоковое видео</a>
    </body>
    </html>
    )rawliteral";

    server.send(200, "text/html", html);
}

// Делает фото и сохраняет на SD карту
void handleCapture() {
    Serial.println("Делаем фото...");
    camera_fb_t * fb = esp_camera_fb_get(); // Получаем кадр с камеры

    if (!fb) {
        Serial.println("Ошибка получения кадра");
        server.send(500, "text/plain", "Ошибка получения кадра");
        return;
    }

    String path = "/photo_" + String(millis()) + ".jpg"; // Генерируем имя файла

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Не удалось открыть файл для записи");
        esp_camera_fb_return(fb);
        server.send(500, "text/plain", "Ошибка открытия файла");
        return;
    }

    size_t written = file.write(fb->buf, fb->len); // Сохраняем кадр
    file.close();
    esp_camera_fb_return(fb);

    if (written == fb->len) {
        Serial.printf("Фото сохранено: %s\n", path.c_str());
        server.send(200, "text/plain", "Фото успешно сохранено: " + path);
    } else {
        Serial.println("Ошибка записи файла");
        server.send(500, "text/plain", "Ошибка записи файла");
    }
}

// Страница со списком фотографий
void handlePhotos() {
    String html = listPhotosHTML();
    server.send(200, "text/html", html);
}

// Вывод конкретного файла (фото) на веб
void handleFile() {
    String path = server.uri(); // URI запроса = путь к файлу
    if(!SD_MMC.exists(path)){
        server.send(404, "text/plain", "Файл не найден");
        return;
    }
    File file = SD_MMC.open(path, "r");
    if(!file){
        server.send(500, "text/plain", "Не удалось открыть файл");
        return;
    }

    server.streamFile(file, "image/jpeg"); // Отправляем файл браузеру
    file.close();
}

// Потоковое видео
void handleStream() {
    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    client.print(response);

    while(client.connected()){
        camera_fb_t * fb = esp_camera_fb_get();
        if(!fb){
            Serial.println("Ошибка получения кадра для стрима");
            break;
        }

        client.printf("--frame\r\n");
        client.printf("Content-Type: image/jpeg\r\n");
        client.printf("Content-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.printf("\r\n");

        esp_camera_fb_return(fb);
        delay(30); // Настройка частоты кадров
    }
}

// ======================= НАСТРОЙКА =======================
void setup() {
    Serial.begin(115200);
    delay(2000);

    // Подключаемся к WiFi
    Serial.println("Подключение к WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("WiFi подключен. IP: ");
    Serial.println(WiFi.localIP());

    // Инициализация SD карты
    if(!SD_MMC.begin()){
        Serial.println("Не удалось инициализировать SD_MMC");
        while(true) delay(1000);
    }
    Serial.println("SD_MMC инициализирована");

    // Инициализация камеры
    initCamera();
    Serial.println("Камера инициализирована");
    // ===================== HTTP маршруты =====================
    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/photos", HTTP_GET, handlePhotos);
    server.on("/stream", HTTP_GET, handleStream);

    // Обработка запросов к фотографиям
    server.onNotFound([](){
        String uri = server.uri();
        if(uri.endsWith(".jpg")){
            handleFile();
        } else {
            server.send(404, "text/plain", "Страница не найдена");
        }
    });

    server.begin();
    Serial.println("HTTP сервер запущен");
}

void loop() {
    server.handleClient(); // Обрабатываем HTTP запросы
}
