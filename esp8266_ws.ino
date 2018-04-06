#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

#define           PIN_LDR           A0    // Light Sensor (photo resistor) is connected to A0
#define           PIN_DHT           5     // a DTH22 is connected to GPIO5
#define           PIN_PIR           13    // PIR sensor
#define           PIN_LED           4

const char        hostname[]      = "esp8266";
const byte        DNS_PORT        = 53;

char              ap_ssid[16]     = {0};
IPAddress         apIP(192, 168, 1, 1);
IPAddress         netMsk(255, 255, 255, 0);

char              wifi_ssid[16]   = {0};
char              wifi_pass[16]   = {0};
unsigned long     wifi_lastCon    = 0;

char              mqtt_server[32] = {0};
int               mqtt_port       = 1883;
boolean           mqtt_secure     = false;
char              mqtt_user[16]   = {0};
char              mqtt_pass[16]   = {0};
char              mqtt_topic[32]  = {0};
char              mqtt_temp[32]   = {0};
char              mqtt_hum[32]    = {0};
char              mqtt_lum[32]    = {0};
char              mqtt_mtn[32]    = {0};
int               mqtt_syncFreq   = 60;             // how often we have to send MQTT message
unsigned long     mqtt_lastPost   = 0;
char              buf[12];

float             temp            = 0;
float             hum             = 0; 
int               minLSValue      = 70;
int               maxLSValue      = 970;
float             LSValue         = 0;
int               state           = 0;
int               prevState       = 0;

DNSServer         dnsServer;
ESP8266WebServer  server(80);
WiFiClient        espClient;
PubSubClient      mqtt_client(espClient);
DHT               dht(PIN_DHT, DHT22);
  
void setup() {   
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DHT, INPUT);
  pinMode(PIN_PIR, INPUT);
    
  digitalWrite(PIN_LED, LOW);
     
  Serial.begin(115200);
  delay(100);
  Serial.println("Setup...");

  sprintf(ap_ssid, "IoT-%06X", ESP.getChipId());
  loadCredentials();
  if (strlen(wifi_ssid) == 0)
  {
    Serial.println("Start AP mode");
    digitalWrite(PIN_LED, HIGH);
      
    WiFi.softAPConfig(apIP, apIP, netMsk);
    
    WiFi.softAP(ap_ssid);  
    delay(500);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
       
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("DNS Server started");
  }
  else
  {
    Serial.println("Start STA mode");
    WiFi.mode(WIFI_STA); 
    WiFi.begin (wifi_ssid, wifi_pass);
  
    mqtt_client.setServer(mqtt_server, mqtt_port);

    sprintf(mqtt_topic, "%s%c%06X", ESP.getChipId());
    if (strlen(mqtt_user) != 0) 
    { 
      strcat(mqtt_topic, mqtt_user);
      strcat(mqtt_topic, "/");    
    }
    sprintf(mqtt_temp, "%s%s", mqtt_topic, "/temperature");
    sprintf(mqtt_hum, "%s%s", mqtt_topic, "/humidity");
    sprintf(mqtt_lum, "%s%s", mqtt_topic, "/luminosity");
    sprintf(mqtt_mtn, "%s%s", mqtt_topic, "/motion");    
  }

  server.on("/", handleRoot);
  server.on("/wifisave", handleWifiSave);
  server.onNotFound ( handleNotFound );
        
  server.begin();
  Serial.println("HTTP Server started");
}

void loop() {
  if (strlen(wifi_ssid) == 0)
  {
    dnsServer.processNextRequest();
  }
  else
  {    
    if (WiFi.status() != WL_CONNECTED)
    {
      if (millis() > (wifi_lastCon + 60000) || (wifi_lastCon == 0)) 
      {    
        //reconnect WiFi 
        Serial.println("");
        Serial.println("Connect Wi-Fi");
        WiFi.disconnect();
        delay(250);
        WiFi.begin (wifi_ssid, wifi_pass);
        wifi_lastCon = millis();
      }
      digitalWrite(PIN_LED, HIGH);
      delay(250);
      digitalWrite(PIN_LED, LOW);
      delay(250);
      Serial.print(".");
    } 
    else 
    {
      wifi_lastCon = millis();

      if (!mqtt_client.connected()) 
      { 
        Serial.println("");
        Serial.println("Connect MQTT");          
        if (mqtt_client.connect(ap_ssid)) 
        {
          Serial.println("MQTT connected");
        } 
        else 
        {
          Serial.print("MQTT failed, rc=");
          Serial.print(mqtt_client.state());    
        }
      }

      if (state != prevState)
      {
        if (mqtt_client.connected()) 
        {
          sprintf(buf, "%d", state);
          mqtt_client.publish(mqtt_mtn, buf);
          mqtt_client.loop();
          prevState = state;
        }
      }
      
      if ((millis() > (mqtt_lastPost + mqtt_syncFreq * 1000)) || (mqtt_lastPost == 0))
      {
        if (mqtt_client.connected()) 
        {
          mqtt_lastPost = millis();
                    
          LSValue = analogRead(PIN_LDR);      
          Serial.print("LSValue: ");
          Serial.println(LSValue);  
              
          if(minLSValue>LSValue) minLSValue=LSValue;
          if(maxLSValue<LSValue) maxLSValue=LSValue;
          LSValue = ((LSValue - minLSValue) * 100 / (maxLSValue - minLSValue));
          Serial.print("Lum: ");
          Serial.println(LSValue);   
          sprintf(buf, "%.0f", LSValue);  
          mqtt_client.publish(mqtt_lum, buf);
          
          temp = dht.readTemperature();
          delay(500);
          if (!isnan(temp)) {
            Serial.print("Temp: ");
            Serial.println(temp);    
            sprintf(buf, "%.2f", temp);
            mqtt_client.publish(mqtt_temp, buf);   
            mqtt_client.loop();  
          }

          hum = dht.readHumidity();
          delay(500);
          if (!isnan(hum)) {
            Serial.print("Hum: ");
            Serial.println(hum);  
            sprintf(buf, "%.2f", hum);
            mqtt_client.publish(mqtt_temp, buf);   
            mqtt_client.loop(); 
          }

          digitalWrite(PIN_LED, HIGH);
          delay(250);
          digitalWrite(PIN_LED, LOW);
          delay(250);
        }
      }
    }
  }

  state = digitalRead(PIN_PIR);
  delay(10);
  digitalWrite(PIN_LED, state);

  //HTTP
  server.handleClient();
}

