#include <Arduino.h>
#include <LittleFS.h>               //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <SPI.h>
#include <LoRa.h>
#include <UniversalTelegramBot.h>

/***************************************************************************** 
* Constants
*****************************************************************************/
#define NSS 0
#define RESET 4
#define DIO0 5
#define DEBUG_LOG

/***************************************************************************** 
* global variables
*****************************************************************************/
//Checks for new messages every 1 second.
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;
int counter = 0;
//define your default values here, if there are different values in config.json, they are overwritten.
char bot_token[60];
char chat_id1[20];
char chat_id2[20];
//flag for saving data
bool shouldSaveConfig = false;

/***************************************************************************** 
* constructors
*****************************************************************************/
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(bot_token, clientTCP);

/***************************************************************************** 
* definitons
*****************************************************************************/
void bot_setup(void);
//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
void handleNewMessages(int numNewMessages);

/***************************************************************************** 
* setup
*****************************************************************************/
void setup() {
  Serial.begin(115200);  

  configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
  clientTCP.setTrustAnchors(&cert); // Add root certificate for api.telegram.org  
  while (!Serial);

  // Set LED Flash as output
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);

  Serial.println("LoRa Initialising");
  LoRa.setPins(NSS, RESET, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  //read configuration from FS json
  Serial.println("mounting FS...");
  
  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          strcpy((char*)bot_token, json["bot_token"]);
          strcpy(chat_id1, json["chat_id1"]);
          strcpy(chat_id2, json["chat_id2"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter telegram_bot_token("token", "telegram token", (char*)bot_token, 60);
  WiFiManagerParameter chat_id_self("chatid1", "chat id", chat_id1, 20);
  WiFiManagerParameter chat_id_group("chatid2", "channel id", chat_id2, 20);
  //add all your parameters here
  wifiManager.addParameter(&telegram_bot_token);
  wifiManager.addParameter(&chat_id_self);
  wifiManager.addParameter(&chat_id_group);

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "EVChrgBot"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("EVChrgBot")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  /*
  WiFi.begin();
  WiFi.config(IPAddress(192, 168, 1, 148), IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
  */
  //if you get here you have connected to the WiFi
  Serial.println("connected:)");

  //read updated parameters
  strcpy((char*)bot_token, telegram_bot_token.getValue());
  strcpy(chat_id1, chat_id_self.getValue());
  strcpy(chat_id2, chat_id_group.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tTelegram bot token : " + String((char*)bot_token));
  Serial.println("\tYour chat id : " + String(chat_id1));
  Serial.println("\tChannel chat id : " + String(chat_id2));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif
    json["bot_token"] = (char*)bot_token;
    json["chat_id1"] = chat_id1;
    json["chat_id2"] = chat_id2;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
#else
    json.printTo(Serial);
    json.printTo(configFile);
#endif
    configFile.close();
    Serial.println("saved to config file");
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  
  // Check NTP/Time, usually it is instantaneous and you can delete the code below.
  Serial.print("Retrieving time: ");
  time_t now = time(nullptr);
  Serial.println(now);
  while (now < 24 * 3600)
  {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  //Update bot token to the newly obtained value
  bot.updateToken(bot_token);
  
  Serial.printf("bot_token updated to %s",bot_token);
  bot.sendMessage(chat_id2, "EV Charging Bot started up", "");

}

/***************************************************************************** 
* main loop
*****************************************************************************/
void loop() {
  
  if (millis() > lastTimeBotRan + botRequestDelay)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);   
    while (numNewMessages) {
#ifdef DEBUG_LOG
      Serial.println("got response");
#endif
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  /**
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // send packet
  LoRa.beginPacket();
  LoRa.print("hello ");
  LoRa.print(counter);
  LoRa.endPacket();

  counter++;

  delay(5000);

  **/
}



/***************************************************************************** 
* Function Defenitions
*****************************************************************************/

/*****************************************************************************
 * FUNCTION
 *  bot_setup
 * DESCRIPTION
 *  Set suggestion for bot in telegram App
 * PARAMETERS
 *  void
 * RETURNS
 *  msg
 *****************************************************************************/
void lora_send(char* msg)
{
  // send packet
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.print(counter);
  LoRa.endPacket();
}

/*****************************************************************************
 * FUNCTION
 *  bot_setup
 * DESCRIPTION
 *  Set suggestion for bot in telegram App
 * PARAMETERS
 *  void
 * RETURNS
 *  void
 *****************************************************************************/
void bot_setup()
{
  const String commands = F("["
                            "{\"command\":\"help\",  \"description\":\"Get bot usage help\"},"
                            "{\"command\":\"startCharging\", \"description\":\"Start EV charging\"},"
                            "{\"command\":\"stopCharging\",\"description\":\"Stop EV charging\"}" // no comma on last command
                            "]");
  //bot.setMyCommands(commands);
  //bot.sendMessage("25235518", "Hola amigo!", "Markdown");
}


/*****************************************************************************
 * FUNCTION
 *  handleNewMessages
 * DESCRIPTION
 *  Check whether telegram bot has recieved any new messagr
 * PARAMETERS
 *  void
 * RETURNS
 *  numNewMessages
 *****************************************************************************/
void handleNewMessages(int numNewMessages)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/

	  /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/ 
#ifdef DEBUG_LOG
  Serial.print("Handle New Messages: ");
  Serial.println(numNewMessages);
#endif

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if ((chat_id != chat_id1) && (chat_id != chat_id2)){
      bot.sendMessage(chat_id, "Sorry, You have no authorization for this action !", "");
      continue;
    }
    /***
    //Eco Channel msg
    if (bot.messages[i].type == "channel_post")
    {
      bot.sendMessage(bot.messages[i].chat_id, bot.messages[i].chat_title + " " + bot.messages[i].text, "");
    }
    
    ***/
    // Print the received message
    String text = bot.messages[i].text;
#ifdef DEBUG_LOG
    Serial.println(text);
#endif
    
    String from_name = bot.messages[i].from_name;
    if ((text == "/help") || (text == "/help@esp32GardenBot")) {
      String welcome = "Hi, " + from_name + ".";
      welcome += "I am your EvCharging Bot\n";
      welcome += "Use the following commands to interact with me: \n";
      welcome += "/startCharging   : To start charging\n";
      welcome += "/stopCharging : To stops charging\n";
      bot.sendMessage(chat_id, welcome, "");
    }
    if ((text == "/startCharging") || (text == "/startCharging@esp32GardenBot")) {
      digitalWrite(LED_BUILTIN, LOW);
      bot.sendMessage(chat_id, "Copy that! Charging started.", "");
      lora_send("11");
#ifdef DEBUG_LOG
      Serial.println("Change flash LED state");
#endif
    }
    if ((text == "/stopCharging") || (text == "/stopCharging@esp32GardenBot")){
      digitalWrite(LED_BUILTIN, HIGH);
      bot.sendMessage(chat_id, "Copy that! Charging stopped.", "");
      lora_send("0");
#ifdef DEBUG_LOG
      Serial.println("Change flash LED state");
#endif
    }
  }
}
