#include "vstp.h"
#include "credentials.h"

#include <ESP8266WiFi.h>

static vstp_state_t vstp_state;


int uart_read()
{
    return Serial.read();
}


void setup()
{
    Serial.setRxBufferSize(256);
    Serial.begin(115200);
    Serial.println("Booting up...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Connecting to WiFi %s", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.printf("\nConnected with IP: %s\n", WiFi.localIP().toString().c_str());

    vstp_init(&vstp_state, uart_read);
}

void loop()
{
    vstp_update(&vstp_state);
}
