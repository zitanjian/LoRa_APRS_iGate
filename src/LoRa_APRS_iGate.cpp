#include <Arduino.h>
#include <WiFiMulti.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <APRS-IS.h>

#include "LoRa_APRS.h"

#include "settings.h"
#include "display.h"
#include "power_management.h"

WiFiMulti WiFiMulti;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, 60*60);
APRS_IS aprs_is(USER, PASS, TOOL, VERS);
#if defined(ARDUINO_T_Beam) && !defined(ARDUINO_T_Beam_V0_7)
PowerManagement powerManagement;
#endif
LoRa_APRS lora_aprs;

int next_update = -1;

void setup_wifi();
void setup_ota();
void setup_lora();
void setup_ntp();

String BeaconMsg;

// cppcheck-suppress unusedFunction
void setup()
{
	Serial.begin(115200);

#if defined(ARDUINO_T_Beam) && !defined(ARDUINO_T_Beam_V0_7)
	Wire.begin(SDA, SCL);
	if (!powerManagement.begin(Wire))
	{
		Serial.println("LoRa-APRS / Init / AXP192 Begin PASS");
	} else {
		Serial.println("LoRa-APRS / Init / AXP192 Begin FAIL");
	}
	powerManagement.activateLoRa();
	powerManagement.activateOLED();
	powerManagement.deactivateGPS();
#endif

	setup_display();
	
	delay(500);
	Serial.println("[INFO] LoRa APRS iGate by OE5BPA (Peter Buchegger)");
	show_display("OE5BPA", "LoRa APRS iGate", "by Peter Buchegger", 2000);

	setup_wifi();
	setup_ota();
	setup_lora();
	setup_ntp();

	APRSMessage msg;
	msg.setSource(USER);
	msg.setDestination("APLG0");
	msg.getAPRSBody()->setData(String("=") + BEACON_LAT_POS + "I" + BEACON_LONG_POS + "&" + BEACON_MESSAGE);
	BeaconMsg = msg.encode();
	
	delay(500);
}

// cppcheck-suppress unusedFunction
void loop()
{
	timeClient.update();
	ArduinoOTA.handle();
	if(WiFiMulti.run() != WL_CONNECTED)
	{
		Serial.println("[ERROR] WiFi not connected!");
		show_display("ERROR", "WiFi not connected!");
		delay(1000);
		return;
	}
	if(!aprs_is.connected())
	{
		Serial.print("[INFO] connecting to server: ");
		Serial.print(SERVER);
		Serial.print(" on port: ");
		Serial.println(PORT);
		show_display("INFO", "Connecting to server");
#ifdef FILTER
		if(!aprs_is.connect(SERVER, PORT, FILTER))
#else
		if(!aprs_is.connect(SERVER, PORT))
#endif
		{
			Serial.println("[ERROR] Connection failed.");
			Serial.println("[INFO] Waiting 5 seconds before retrying...");
			show_display("ERROR", "Server connection failed!", "waiting 5 sec");
			delay(5000);
			return;
		}
		Serial.println("[INFO] Connected to server!");
	}
	if(next_update == timeClient.getMinutes() || next_update == -1)
	{
		show_display(USER, "Beacon to Server...");
		Serial.print("[" + timeClient.getFormattedTime() + "] ");
		aprs_is.sendMessage(BeaconMsg);
		next_update = (timeClient.getMinutes() + BEACON_TIMEOUT) % 60;
	}
	if(aprs_is.available() > 0)
	{
		String str = aprs_is.getMessage();
		Serial.print("[" + timeClient.getFormattedTime() + "] ");
		Serial.println(str);
#ifdef FILTER
		show_display(USER, timeClient.getFormattedTime() + "    IS-Server", str, 0);
#endif
#ifdef SEND_MESSAGES_FROM_IS_TO_LORA
		std::shared_ptr<APRSMessage> msg = std::shared_ptr<APRSMessage>(new APRSMessage());
		msg->decode(str);
		lora_aprs.sendMessage(msg);
#endif
	}
	if(lora_aprs.hasMessage())
	{
		std::shared_ptr<APRSMessage> msg = lora_aprs.getMessage();

		show_display(USER, timeClient.getFormattedTime() + "         LoRa", "RSSI: " + String(lora_aprs.getMessageRssi()) + ", SNR: " + String(lora_aprs.getMessageSnr()), msg->toString(), 0);
		Serial.print("[" + timeClient.getFormattedTime() + "] ");
		Serial.print(" Received packet '");
		Serial.print(msg->toString());
		Serial.print("' with RSSI ");
		Serial.print(lora_aprs.getMessageRssi());
		Serial.print(" and SNR ");
		Serial.println(lora_aprs.getMessageSnr());

		aprs_is.sendMessage(msg->encode());
	}
}

void setup_wifi()
{
	WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
	WiFi.setHostname(USER);
	WiFiMulti.addAP(WIFI_NAME, WIFI_KEY);
	Serial.print("[INFO] Waiting for WiFi");
	show_display("INFO", "Waiting for WiFi");
	while(WiFiMulti.run() != WL_CONNECTED)
	{
		Serial.print(".");
		show_display("INFO", "Waiting for WiFi", "....");
		delay(500);
	}
	Serial.println("");
	Serial.println("[INFO] WiFi connected");
	Serial.print("[INFO] IP address: ");
	Serial.println(WiFi.localIP());
	show_display("INFO", "WiFi connected", "IP: ", WiFi.localIP().toString(), 2000);
}

void setup_ota()
{
	ArduinoOTA
		.onStart([]()
		{
			String type;
			if (ArduinoOTA.getCommand() == U_FLASH)
				type = "sketch";
			else // U_SPIFFS
				type = "filesystem";
			Serial.println("Start updating " + type);
			show_display("OTA UPDATE", "Start update", type);
		})
		.onEnd([]()
		{
			Serial.println();
			Serial.println("End");
		})
		.onProgress([](unsigned int progress, unsigned int total)
		{
			Serial.print("Progress: ");
			Serial.print(progress / (total / 100));
			Serial.println("%");
			show_display("OTA UPDATE", "Progress: ", String(progress / (total / 100)) + "%");
		})
		.onError([](ota_error_t error) {
			Serial.print("Error[");
			Serial.print(error);
			Serial.print("]: ");
			if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
			else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
			else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
			else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
			else if (error == OTA_END_ERROR) Serial.println("End Failed");
		});
	ArduinoOTA.setHostname(USER);
	ArduinoOTA.begin();
}

void setup_lora()
{
	Serial.print("[INFO] frequency: ");
	Serial.println(LORA_RX_FREQUENCY);
	
	if (!lora_aprs.begin())
	{
		Serial.println("[ERROR] Starting LoRa failed!");
		show_display("ERROR", "Starting LoRa failed!");
		while (1);
	}
	Serial.println("[INFO] LoRa init done!");
	show_display("INFO", "LoRa init done!", 2000);
}

void setup_ntp()
{
	timeClient.begin();
	if(!timeClient.forceUpdate())
	{
		Serial.println("[WARN] NTP Client force update issue!");
		show_display("WARN", "NTP Client force update issue!", 2000);
	}
	Serial.println("[INFO] NTP Client init done!");
	show_display("INFO", "NTP Client init done!", 2000);
}
