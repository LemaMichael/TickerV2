#include "ESP8266WiFi.h"
#include "WiFiClientSecure.h"
#include "ArduinoJson.h"
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <stdio.h>


// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// We always wait a bit between updates of the display
#define  DELAYTIME  100  // in milliseconds

const char* ssid = "";
const char* password = "";

const char* coinbaseHost = "api.pro.coinbase.com";
const char* coinbaseFingerprint = "9C B0 72 05 A4 F9 D7 4E 5A A4 06 5E DD 1F 1C 27 5D C2 F1 48";

const char* binanceHost = "api.binance.com";
const char* binanceFingerprint = "41 82 D2 BA 64 E3 36 F1 3C 5E 49 05 2A A0 AA CB D0 F7 2B B7";

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);

void scrollText(char *p) {
    uint8_t charWidth;
    uint8_t cBuf[8];  // this should be ok for all built-in fonts
    mx.clear();
    
    while (*p != '\0')
    {
        charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        
        for (uint8_t i=0; i<=charWidth; i++)  // allow space between characters
        {
            mx.transform(MD_MAX72XX::TSL);
            if (i < charWidth)
                mx.setColumn(0, cBuf[i]);
            delay(DELAYTIME);
        }
    }
}

void connectToWIFI() {
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Start the server
    server.begin();
    Serial.println(F("Server Started"));
}


JsonObject& getJsonObject(String url, bool isCoinbaseCoin) {
    const size_t capacity = (isCoinbaseCoin) ? JSON_OBJECT_SIZE(7) + 252 : JSON_OBJECT_SIZE(2) + 74;
    DynamicJsonBuffer jsonBuffer(capacity);
    
    // Use WiFiClientSecure class to create TLS connection
    WiFiClientSecure client;
    client.setTimeout(10000);
    
    const char* host = (isCoinbaseCoin) ? coinbaseHost : binanceHost;
    const char* fingerprint = (isCoinbaseCoin) ? coinbaseFingerprint : binanceFingerprint;
    Serial.println(host);
    Serial.printf("Using fingerprint '%s'\n", fingerprint);
    client.setFingerprint(fingerprint);
    
    if (!client.connect(host, 443)) {
        Serial.println("connection failed");
        scrollText("Connection failed!");
        // No further work should be done if the connection failed
        return jsonBuffer.parseObject(client);
    }
    Serial.println(F("Connected!"));
    
    // Send HTTP Request
    String httpEnding = (isCoinbaseCoin) ? " HTTP/1.1\r\n" : " HTTP/1.0\r\n";
    client.print(String("GET ") + url + httpEnding +
                 "Host: " + host + "\r\n" +
                 "User-Agent: BuildFailureDetectorESP8266\r\n" +
                 "Connection: close\r\n\r\n");
    Serial.println("request sent");
    
    // Check HTTP Status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
        Serial.print(F("Unexpected response: "));
        Serial.println(status);
        return jsonBuffer.parseObject(client);
    }
    
    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!client.find(endOfHeaders)) {
        Serial.println(F("Invalid response"));
        //scrollText("Invalid Response");
    }
    
    //Read reply from server
    //    Serial.println("[Response:]");
    //    while (client.connected() || client.available()) {
    //        if (client.available()) {
    //            String line = client.readStringUntil('\n');
    //            Serial.println(line);
    //        }
    //    }
    
    // Parse JSON object
    JsonObject& root = jsonBuffer.parseObject(client);
    if (!root.success()) {
        Serial.println(F("Parsing failed!"));
        //scrollText("JSON Parse Failed!");
    }
    
    // Disconnect
    client.stop();
    jsonBuffer.clear();
    return root;
}

float bitcoinPrice = 0.0;

float convertToUSD(float cryptoPrice) {
    if (bitcoinPrice == 0.0) { getBitcoinPrice(); }
    return bitcoinPrice * cryptoPrice;
}

void getBitcoinPrice() {
    JsonObject& root = getJsonObject("/api/v1/ticker/price?symbol=BTCUSDC", false);
    Serial.print("The Bitcoin price is:");
    Serial.println(root["price"].as<float>());
    bitcoinPrice = root["price"].as<float>();
}

void getCoinPrice(String url, String cryptoName, bool isCoinbaseCoin) {
    JsonObject& root = getJsonObject(url, isCoinbaseCoin);
    Serial.println("==========");
    Serial.println(F("Response:"));
    Serial.print("Symbol: ");
    Serial.println(root["symbol"].as<char*>());
    Serial.print("Price: ");
    Serial.println(root["price"].as<char*>());
    
    float cryptoPrice = root["price"].as<float>();
    cryptoPrice = (isCoinbaseCoin) ? cryptoPrice : convertToUSD(cryptoPrice);
    Serial.println(cryptoPrice);
    Serial.println("==========");
    String output = cryptoName + " $" + String(cryptoPrice);
    Serial.println(output);
    
    // Update the bitcoinPrice if the User requests the Bitcoin Price
    bitcoinPrice = (cryptoName == "BTC") ? cryptoPrice : bitcoinPrice;
    
    char *cstr = new char[output.length() + 1];
    strcpy(cstr, output.c_str());
    scrollText(cstr);
    delete [] cstr;
}

void setup() {
    // put your setup code here, to run once:
    mx.begin();
    Serial.begin(115200);
    connectToWIFI();
}

void loop() {
    // Check if a client has connected
    WiFiClient client = server.available();
    if(!client) {
        return;
    }
    Serial.println(F("new client"));
    client.setTimeout(5000); // Default is 1000
    
    // Read the first line of the request
    String req = client.readStringUntil('\r');
    Serial.println(F("Request: "));
    Serial.println(req);
    
    String coinURL;
    String coinName;
    bool isCoinbaseCoin;
    
    // Match the request
    if (req.indexOf("/products") != -1) {
        // Example of req: GET /products/BTC-USD/ticker HTTP/1.1
        isCoinbaseCoin = true;
        coinURL = req.substring(req.indexOf(' ') + 1, req.lastIndexOf('r') + 1);
        coinName = req.substring(14, req.indexOf('-'));
        Serial.println("Received coinbase Request! " + coinURL);
    } else if (req.indexOf("/api/v1") != -1) {
        isCoinbaseCoin = false;
        // Example of req: GET /api/v1/ticker/price?symbol=XMRBTC HTTP/1.1
        coinURL = req.substring(req.indexOf(' ') + 1, req.lastIndexOf('C') + 1);
        coinName = req.substring(req.indexOf('=') + 1, req.lastIndexOf('B'));
        Serial.println("Received Binance Request! " + coinURL);
    }
    else {
        Serial.println(F("Invalid Request"));
        return;
    }
    getCoinPrice(coinURL, coinName, isCoinbaseCoin);
    
    while (client.available()) {
        // byte by byte is not very efficient
        client.read();
    }
    
    // Send the response to the client
    // it is OK for multiple small client.print/write,
    // because nagle algorithm will group them into one single packet
    client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nGPIO is now "));
    client.print(F("<br><br>Click <a href='http://"));
    client.print(WiFi.localIP());
    client.print(F("/gpio/1'>here</a> to switch LED GPIO on, or <a href='http://"));
    client.print(WiFi.localIP());
    client.print(F("/gpio/0'>here</a> to switch LED GPIO off.</html>"));
    
    // The client will actually be *flushed* then disconnected
    // when the function returns and 'client' object is destroyed (out-of-scope)
    // flush = ensure written data are received by the other side
    Serial.println(F("Disconnecting from client"));
}
