/*******************************************************************
 *  A project to light up leds based on the current traffic       
 *  conditions on the Golden Gate Bridge.                          
 *  Traffic data is being sourced from Google Maps
 *  
 *  Main Hardware:
 *  - ESP8266
 *  - Neopixels
 *                                                                 
 *  Written by Brian Lough                                         
 *******************************************************************/

// ----------------------------
// Standard Libraries
// ----------------------------

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include "FS.h"

// ----------------------------
// Additional libraries - each one of these will need to be installed.
// ----------------------------

#include <GoogleMapsApi.h>
// For accessing Google Maps Api
// Availalbe on library manager (GoogleMapsApi)
// https://github.com/witnessmenow/arduino-google-maps-api

#include <ArduinoJson.h>
// For parsing the response from google maps and for the config file
// Available on the library manager (ArduinoJson)
// https://github.com/bblanchon/ArduinoJson

#include <Adafruit_NeoPixel.h>
// For controlling the Addressable LEDs
// Available on the library manager (Adafruit Neopixel)
// https://github.com/adafruit/Adafruit_NeoPixel

#include <NTPClient.h>
// For keeping the time, incase we want to do anything based on time
// Available on the library manager (NTPClient)
// https://github.com/arduino-libraries/NTPClient


#define GREEN_COLOUR_INDEX 0
#define YELLOW_COLOUR_INDEX 1
#define RED_COLOUR_INDEX 2

#define GREEN_VAL 0, 255, 0
#define YELLOW_VAL 255, 100, 0
#define RED_VAL 255, 0, 0

#define LIGHT_GREEN_VAL 50, 200, 50
#define LIGHT_YELLOW_VAL 255, 130, 40
#define LIGHT_RED_VAL 200, 50, 50

// ----------------------------
// Change the following to adapt for you
// ----------------------------

// Set between 0 and 255, 255 being the brigthest
#define BRIGTHNESS 16

// Server to get the time off
// See here for a list: http://www.pool.ntp.org/en/
const char timeServer[] = "us.pool.ntp.org";

// If the travel time is longer than normal + MEDIUM_TRAFFIC_THRESHOLD, light the route ORANGE
// Value is in seconds
#define MEDIUM_TRAFFIC_THRESHOLD 60

// If the travel time is longer than normal + BAD_TRAFFIC_THRESHOLD, light the route RED
// Value is in seconds (5 * 60 = 300)
#define BAD_TRAFFIC_THRESHOLD 300

// Default Traffic-Matrix API key, you can set this if you want or put it in using the WiFiManager
char apiKey[45] = "ENTER API KEY HERE";

//Free Google Maps Api only allows for 2500 "elements" a day
unsigned long delayBetweenApiCalls = 1000 * 60; // 1 mins

unsigned long delayBetweenLedChange = 1000 * 10; // 10 seconds

// You also may need to change the colours in the getRouteColour method.
// The leds I used seemed to have the Red colour and Green colour swapped in comparison 
// to the library's documentation.

// ----------------------------
// End of area you need to change
// ----------------------------

WiFiUDP ntpUDP;

//Probably will need to change the offset for Pacific time
NTPClient timeClient(ntpUDP, timeServer, 3600, 60000);

// Number of seconds after reset during which a 
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

WiFiClientSecure client;
GoogleMapsApi *mapsApi;

unsigned long api_due_time = 0;

unsigned long led_due_time = 0;

void connectWIFI() {
  const char* ssid     = "ENTER WIFI SSID HERE";
  const char* password = "ENTER WIFI PASSWORD HERE";
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

class Strip {
  public:
    uint32_t getColour(int durationTraffic_value, int duration_value) {

      int difference = durationTraffic_value - duration_value;
      Serial.print("durationTraffic_value: ");
      Serial.println(durationTraffic_value);

      Serial.print("duration_value: ");
      Serial.println(duration_value);

      Serial.print("difference: ");
      Serial.println(difference);

      Serial.print("BAD_TRAFFIC_THRESHOLD: ");
      Serial.println(BAD_TRAFFIC_THRESHOLD);

      Serial.print("MEDIUM_TRAFFIC_THRESHOLD: ");
      Serial.println(MEDIUM_TRAFFIC_THRESHOLD);

      if(difference > BAD_TRAFFIC_THRESHOLD) {
        Serial.println("Setitng Colour to red");
        colourIndex = RED_COLOUR_INDEX;
        return leds.Color(RED_VAL); // Red
      } else if ( difference > MEDIUM_TRAFFIC_THRESHOLD ) {
        Serial.println("Setitng Colour to yellow");
        colourIndex = YELLOW_COLOUR_INDEX;
        return leds.Color(YELLOW_VAL); // Yellow
      }
      Serial.println("Setitng Colour to Green");
      colourIndex = GREEN_COLOUR_INDEX;
      return leds.Color(GREEN_VAL); //Green
    }

    void unLightAllLeds() {
      for(int i=0; i< NUMBER_OF_LEDS; i++) {
        leds.setPixelColor(i, leds.Color(0, 0, 0));
      }
      leds.show();
    }

    void setAllLeds(uint32_t col) {
      for(int i=0; i< NUMBER_OF_LEDS; i++) {
        leds.setPixelColor(i, col);
      }
    }

    void lightLeds() {
      unLightAllLeds();
      for (int j = 1; j <= NUMBER_OF_LEDS; j++) {
        for (int i = 0; i < j; i++) {
          leds.setPixelColor(i, colour);
        }
        leds.show();
        delay(100);
      }

      // having the double for loop and the leds.show and delay inside the outer one
      // is to create the one after another effect, that i think is quite nice
      // its a good way of indicating which way it is checking traffic and also
      // when it refreshed. We may need to change this based on how the LEDS are wired.
      // comment out leds.show() and the delay from above and uncomment the line below to remove that feature
      //leds.show()
    }

    void lightLedsForwards(uint32_t newColour, uint32_t oldColour){
      for (int j = 1; j <= NUMBER_OF_LEDS; j++) {
        setAllLeds(oldColour);
        for (int i = 0; i < j; i++) {
          leds.setPixelColor(i, newColour);
        }
        leds.show();
        delay(200);
      }
    }

    void lightLedsBackwards(uint32_t newColour, uint32_t oldColour){
      for (int j = NUMBER_OF_LEDS - 1; j >= 0 ; j--) {
        setAllLeds(oldColour);
        for (int i = NUMBER_OF_LEDS - 1; i >= j; i--) {
          leds.setPixelColor(i, newColour);
        }
        leds.show();
        delay(200);
      }
    }

    void twinkleLed(){
      uint32_t lighterColour = leds.Color(0, 0, 0);
      switch(colourIndex){
        case GREEN_COLOUR_INDEX:
          {
            lighterColour = leds.Color(LIGHT_GREEN_VAL);
          }
          break;
        case YELLOW_COLOUR_INDEX:
          {
            lighterColour = leds.Color(LIGHT_YELLOW_VAL);
          }
          break;
         
        case RED_COLOUR_INDEX:
          {
            lighterColour = leds.Color(LIGHT_RED_VAL);
          }
          break;
      }

      lightLedsForwards(lighterColour, colour);
      delay(600);
      lightLedsBackwards(colour, lighterColour);
    }

    bool checkGoogleMaps() {
      Serial.println("Getting traffic for " + origin + " to " + destination);
        String responseString = mapsApi->distanceMatrix(origin, destination, "now");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& response = jsonBuffer.parseObject(responseString);
        if (response.success()) {
          if (response.containsKey("rows")) {
            JsonObject& element = response["rows"][0]["elements"][0];
            String status = element["status"];
            if(status == "OK") {

              int durationInSeconds = element["duration"]["value"];
              int durationInTrafficInSeconds = element["duration_in_traffic"]["value"];
              colour = getColour(durationInTrafficInSeconds, durationInSeconds);
              Serial.println("Duration In Traffic:  " + durationInTrafficInSeconds);
              return true;

            }
            else {
              Serial.println("Got an error status: " + status);
              return false;
            }
          } else {
            Serial.println("Reponse did not contain rows");
            return false;
          }
        } else {
          if(responseString == ""){
            Serial.println("No response, probably timed out");
          } else {
            Serial.println("Failed to parse Json. Response:");
            Serial.println(responseString);
          }

          return false;
        }

        return false;
    }

    Strip(int p, int n):
      LED_PIN(p),
      NUMBER_OF_LEDS(n),
      leds(NUMBER_OF_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800) {}

    void initialize(String o, String d) {
      leds.begin(); // This initializes the NeoPixel library.
      leds.setBrightness(BRIGTHNESS);
      unLightAllLeds();
      colour = leds.Color(255, 0, 0);
      setLocations(o, d);
    }
  private:
    uint32_t LED_PIN;
    uint32_t NUMBER_OF_LEDS;
    Adafruit_NeoPixel leds;
    String origin;
    String destination;
    uint32_t colour;
    int colourIndex = 0;
    
    void setLocations(String o, String d) {
      origin = o;
      destination = d;
    }
};

Strip strip1(1, 5);
Strip strip3(3, 5);
Strip strip4(4, 5);
Strip strip5(5, 5);

void setup() {
  Serial.begin(115200);

  connectWIFI();

  // Denmark
  strip1.initialize("56.168580,10.116812", "56.171801,10.187780");

  // Shiraz
  strip3.initialize("29.636075,52.480785", "29.633292,52.507145");

  // Tehran
  strip4.initialize("35.730466,51.298777", "35.701124,51.396211");

  // SF Bay area
  strip5.initialize("37.349147,-121.994421", "37.367907,-121.916010");
  
  mapsApi = new GoogleMapsApi(apiKey, client);
  timeClient.begin();
}

void loop() {
  unsigned long timeNow = millis();
  if (timeNow > api_due_time)  {
    Serial.println("Checking maps");
    if (strip1.checkGoogleMaps()) {
      strip1.lightLeds();
    } 
    if (strip3.checkGoogleMaps()) {
      strip3.lightLeds();
    } 
    if (strip4.checkGoogleMaps()) {
      strip4.lightLeds();
    } 
    if (strip5.checkGoogleMaps()) {
      strip5.lightLeds();
    } 
    api_due_time = timeNow + delayBetweenApiCalls;
    led_due_time = timeNow + delayBetweenLedChange;
  }
  timeNow = millis();
  if (timeNow > led_due_time)  {
    Serial.println("Chaning LED");
    strip1.twinkleLed();
    strip3.twinkleLed();
    strip4.twinkleLed();
    strip5.twinkleLed();
    led_due_time = timeNow + delayBetweenLedChange;
  }
}

