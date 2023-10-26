/*
  Project Name: ... Temp Buddy
  Written By: ..... Scott Griffis
  Email: .......... birdoffire1549@gmail.com
  Date: ........... 08-04-2023

  High-level Overview: 
  This code enables an ESP8266MOD to obtain Temperature and Humidity readings
  from a connected AHT10 Device, it then presents the information upon request to any device which 
  connects to its IP Address via port 80. The data is in the form of a web-page with some formatting, 
  and can be displayed in a web browser.

  SECURITY RELATED PLEASE NOTE: 
  Communication with this Device is NOT encrypted. This means that when configuring the settings of the
  device using the Admin page, the data is transmitted to and from the client in clear text on the network you
  and the device are connected to. The potential for compromise is highest when doing initial settings with the
  device in AP mode as there is no password used for the TempBuddy network. Be cautious as this could result in
  the leaking of target network's SSID and Password when initially setup, also if changing the device's admin
  password don't use one that you use for other sensitive things as it will be sent to the device in clear text.
  [TODO: Add SSL usage]

  Hosted Endpoints:
  /        - This is where the temperature and humidity information is deplayed as a web page
  /admin   - This is where the device's settings are configured. Default Password: admin
  /update  - This page isn't intended to be accessed directly rather it should be accessed through the admin page's save functionality

  More Detailed:
  When device is first programmed it boots up as an AccessPoint that can be connected to using a computer, 
  by connecting to the presented network with a name of 'TempBuddy' and no password. Once connected to the
  device's WiFi network you can connect to it for configuration using a web browser via the URL:
  http://192.168.4.1/admin. This will take you to a page requesting a password. Initially the password is
  simply 'admin' but can be changed. This will display the current device settings and allow the user to make desired
  configuration changes to the device. When the Network settings are changed the device will reboot and 
  attempt to connect to the configured network. This code also allows for the device to be equiped with a 
  factory reset button. To perform a factory reset the factory reset button must supply a HIGH to its input
  while the device is rebooted. Upon reboot if the factory reset button is HIGH the stored settings in flash will
  be replaced with the original factory default setttings. The factory reset button also serves another purpose 
  during the normal operation of the device. If pressed breifly the device will flash out the last octet of its 
  IP Address. It does this using the built-in LED. Each digit of the last octet is flashed out with a breif 
  rapid flash between the blink count for each digit. Once all digits have been flashed out the LED will do
  a long rapid flash. Also, one may use the factory reset button to obtain the full IP Address of the device by 
  keeping the ractory reset button pressed during normal device operation for more than 6 seconds. When flashing
  out the IP address the device starts with the first digit of the first octet and flashes slowly that number of 
  times, then it performs a rapid flash to indicate it is on to the next digit. Once all digits in an octet have
  been flashed out the device performs a second after digit rapid flash to indicate it has moved onto a new 
  octet. 
  
  I will demonstrate how this works below by representting a single flash of the LED as a dash '-'. I will represent
  the post digit rapid flash with three dots '...', and finally I will represent the end of sequence long flash
  using 10 dots '..........'. 

  Using the above the IP address of 192.168.123.71 would be flashed out as follows:
  1                     9       2       . 1               6                   8       . 1       2         3       .             7     1           
  - ... - - - - - - - - - ... - - ... ... - ... - - - - - - ... - - - - - - - - ... ... - ... - - ... - - - ... ... - - - - - - - ... - ..........

  The short button press version of the above would simply be the last octet so in this case it would be:
              7     1
  - - - - - - - ... - ..........

  The last octet is useful if you know the network portion of the IP Address the device would be attaching to but
  are not sure what the assigned host portion of the address is, of course this is for network masks of 255.255.255.0.
*/

// ************************************************************************************
// Include Statements
// ************************************************************************************
#include <ESP_EEPROM.h>
#include <ESP8266WiFi.h>
#include <AHT10.h>

// ************************************************************************************
// Constants
// ************************************************************************************
const int LED_PIN = 2; // Output used for flashing out IP Address
const int RESTORE_PIN = 13; // Input used for factory reset button; Normally Low
const char* apSsid = "TempBuddy";
const char* apPwd = "<none>";

// ************************************************************************************
// Structure used for storing of settings related data and persisted into flash
// ************************************************************************************
struct Settings {
  char ssid[32]; // 32 chars is max size
  char pwd[63]; // 63 chars is max size
  char adminPwd[12];
  char title[50];
  char heading[50];
  bool factorySettings;
  unsigned long timeout;
} settings;
 
// ************************************************************************************
// Setup of Services
// ************************************************************************************
WiFiServer server(80);
AHT10 AHT10;

// ************************************************************************************
// Global worker variables
// ************************************************************************************
String ipAddr = "0.0.0.0";

/***************************** 
 * SETUP() - REQUIRED FUNCTION
 * ***************************
 * This function is the required setup() function and it is
 * where the initialization of the application happens. 
 */
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off; High = off and Low = On.
  pinMode(RESTORE_PIN, INPUT);

  // Initialize Serial for debug...
  Serial.begin(115200);
  delay(15);

  // Initialize the device...
  Serial.println(F("\nInitializing device..."));

  resetOrLoadSettings();
  doStartAHT10(); // Temp/Humidity device
  doStartNetwork();

  Serial.println(F("Initialization complete."));
}

/****************************
 * LOOP() - REQUIRED FUNCTION
 * **************************
 * This is the required loop() function of the applicaiton.
 * Here is where all functionality happens or starts to happen.
 */
void loop() {
  checkIpDisplayRequest();

  // Check if a client has connected...
  WiFiClient client = server.accept();
  if (!client) { // No client...
    return;
  } else { // Client connected...
    Serial.println(F("New Client!"));
  }

  client.setTimeout(settings.timeout);
 
  // Read input and parse request...
  String req = client.readStringUntil('\r');
  int space1 = req.indexOf(' ');
  int space2 = -1;
  if (space1 != -1) { // First space was found...
    space2 = req.indexOf(' ', space1 + 1);
  }
  if (space1 != -1 && space2 != -1) { // Both spaces were found...
    req = req.substring(space1 + 1, space2);
  }
  Serial.print(F("Client Request: "));
  Serial.println(req);
  
  // Evaluate and act upon the request...
  if (req == "/") { // <------------------------- ['/']
    showInfoPage(client);
  } else if (req == "/admin") { // <------------- ['/admin']
    String reqBody = client.readString();
    showAdminPage(client, decodeUrlString(reqBody));
  } else if (req == "/update") { // <------------ ['/update']
    String reqBody = client.readString();
    showAndHandleUpdatePage(client, decodeUrlString(reqBody));
  }

  delay(1);
  Serial.println(F("Client disonnected"));
  Serial.println("");
}

/**
 * Check to see if the factory reset pin is being held down during
 * normal operation of the device. If it is, then count for how long
 * it is held down for. If less than 6 seconds then signal the last
 * octet of the IP Address. If longer than 6 seconds then signal the
 * entire IP Address.
 */
void checkIpDisplayRequest() {
  int counter = 0;
  while (digitalRead(RESTORE_PIN) == HIGH) {
    counter++;
    delay(1000);
  }
  if (counter > 0 && counter < 6) {
    signalIpAddress(ipAddr, true);
  } else if (counter >= 6) {
    signalIpAddress(ipAddr, false);
  }
}

/**
 * Puts the device into client mode such that it will
 * connect to a specified WiFi network based on its
 * SSID and Password.
 */
void connectToNetwork() {
  // Connect to WiFi network...
  Serial.print(F("\n\nConnecting to "));
  Serial.print(settings.ssid);
  
  WiFi.begin(settings.ssid, settings.pwd);
  while (WiFi.status() != WL_CONNECTED) { // WiFi is not yet connected...
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println(F("WiFi connected"));
}

/**
 * Puts the device into AP Mode so that user can
 * connect via WiFi directly to the device to configure
 * the device.
 */
void activateAPMode() {
  Serial.print(F("Configuring AP mode... "));
  bool ret = WiFi.softAP(apSsid);
  Serial.println(ret ? F("Complete.") : F("Failed!"));
}

/**
 * This function is used to persist settings to EEPROM.
 */
bool persistSettings() {
  // Serial.print(F("Persisting settings to EEPROM... "));
  EEPROM.wipe(); // usage seemd to grow without this.
  EEPROM.put(0, settings);
  bool ok = EEPROM.commit();
  Serial.println((ok) ? F("Complete.") : F("Failed!"));

  return ok;
}

/**
 * Detects and reacts to a reqest for factory reset
 * during the boot-up. Also loads settings from 
 * EEPROM if there are saved settings.
 */
void resetOrLoadSettings() {
  // Setup default settings...
  // These will be overridden by saved 
  // data if not in factory restore mode...
  strcpy(settings.ssid, "SET_ME");
  strcpy(settings.pwd, "SET_ME");
  strcpy(settings.adminPwd, "admin");
  strcpy(settings.title, "Temp Buddy - IOT");
  strcpy(settings.heading, "Temp Info");
  settings.factorySettings = true;
  settings.timeout = 5000;

  // Setup EEPROM for loading and saving...
  EEPROM.begin(sizeof(Settings));

  // Persist default settings or load settings...
  delay(15);
  if (digitalRead(RESTORE_PIN) == HIGH) { // Restore button pressed on bootup...
    Serial.println(F("\nPerforming Factory Reset..."));
    persistSettings();
    Serial.println(F("Factory reset complete."));
  } else { // Normal load restore pin not pressed...
    // Load from EEPROM if applicable...
    if (EEPROM.percentUsed() >= 0) { // Settings are stored from prior...
      Serial.println(F("\nLoading settings from EEPROM..."));
      EEPROM.get(0, settings);
      Serial.print(F("Percent of ESP Flash currently used is: "));
      Serial.print(EEPROM.percentUsed());
      Serial.println(F("%"));
    }
  }
}

/**
 * Used to startup the network related services.
 * Starts the device in AP mode or connects to 
 * existing WiFi based on settings.
 */
void doStartNetwork() {
  // Connect to wireless or enable AP Mode...
  if (settings.factorySettings) { // In factory restore mode...
    activateAPMode();
  } else { // Normal mode connects to WiFi...
    connectToNetwork();
  }
  
  // Start the server...
  server.begin();
  Serial.println(F("\nServer started."));
  
  if (settings.factorySettings) { // Settings are factory default...
    Serial.println(F("\nConnect to AP:"));
    Serial.print(F("SSID = "));
    Serial.println(apSsid);
    Serial.print(F("Password = "));
    Serial.println(apPwd);
  }

  // Print the IP address...
  Serial.print(F("\nUse this URL to connect: "));
  Serial.print(F("http://"));
  if (settings.factorySettings) { // Factory default settings so use AP IP...
    ipAddr = WiFi.softAPIP().toString();
    Serial.print(ipAddr);
  } else { // Connected to a WiFi so use local IP...
    ipAddr = WiFi.localIP().toString();
    Serial.print(ipAddr);
  }
  Serial.println("/");
}

/**
 * Initializes the AHT10 Temp/Humidity sensor,
 * preparing it for use.
 */
void doStartAHT10() {
  // Initialize the AHT10 Sensor...  
  Serial.print(F("AHT10 initialization... "));
  if (AHT10.begin()) { // Sensor started...
    Serial.println(F("Complete."));
  } else { // Sensor failed...
    Serial.println(F("Failed!"));
  }
}

/**
 * UTILITY FUNCTION
 * This function is used to decode a URL encoded String.
 */
String decodeUrlString(String string) {
  string.replace("%20", " ");
  string.replace("%21", "!");
  string.replace("%23", "#");
  string.replace("%24", "$");
  string.replace("%26", "&");
  string.replace("%27", "'");
  string.replace("%28", "(");
  string.replace("%29", ")");
  string.replace("%2A", "*");
  string.replace("+", " ");
  string.replace("%2B", "+");
  string.replace("%2C", ",");
  string.replace("%2F", "/");
  string.replace("%3A", ":");
  string.replace("%3B", ";");
  string.replace("%3D", "=");
  string.replace("%3F", "?");
  string.replace("%40", "@");
  string.replace("%5B", "[");
  string.replace("%2D", "]");
  string.replace("%22", "\"");
  string.replace("%25", "%");
  string.replace("%2D", "-");
  string.replace("%2E", ".");
  string.replace("%3C", "<");
  string.replace("%3E", ">");
  string.replace("%5C", "\\");
  string.replace("%5E", "^");
  string.replace("%5F", "_");
  string.replace("%60", "`");
  string.replace("%7B", "{");
  string.replace("%7C", "|");
  string.replace("%7D", "}");
  string.replace("%7E", "~");

  return string;
}

/******************************************************
 * INFO/ROOT PAGE
 * ****************************************************
 * This function shows the info page to a given client.
 */
void showInfoPage(WiFiClient client) {
  // Send HTML Header...
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(""); //  do not forget this one
  
  // Build and send Information Page...
  String content = String("")  
    + "Temperature:\t" + ((AHT10.readTemperature() * 9/5) + 32)  + "&deg;F<br>"
    + "Humidity:\t" + AHT10.readHumidity() + "%<br><br>";
  client.println(htmlPageTemplate(settings.title, settings.heading, content));
}

/****************************************************
 * ADMIN PAGE
 * **************************************************
 * This function shows the admin page for the device.
 */
void showAdminPage(WiFiClient client, String bodyIn) {
  // Initial content is the login page unless authenticated
  String content = ""
    "<form name=\"login\" method=\"post\" id=\"login\" action=\"admin\"> "
      "Password: <input type=\"password\" name=\"adminpwd\" id=\"adminpwd\">"
      "<br> "
      "<input type=\"submit\"> <a href='/'><h4>Home Page</h4></a>"
    "</form>"
  ;
  bodyIn.trim();
  int keyIndex = bodyIn.indexOf("adminpwd=");
  if (keyIndex != -1) { // Maybe Authorized...
    String adminPwd = bodyIn.substring(keyIndex + 9);
    if (strcmp(settings.adminPwd, adminPwd.c_str()) == 0) { // Authorized...
      content = ""  
        "<form name=\"settings\" method=\"post\" id=\"settings\" action=\"update\"> "
          "<h2>WiFi</h2> "
          "SSID: <input maxlength=\"32\" type=\"text\" value=\"${ssid}\" name=\"ssid\" id=\"ssid\"> <br> "
          "Password: <input maxlength=\"63\" type=\"text\" value=\"${pwd}\" name=\"pwd\" id=\"pwd\"> <br> "
          "<h2>Application</h2> "
          "Title: <input maxlength=\"50\" type=\"text\" value=\"${title}\" name=\"title\" id=\"title\"> <br> "
          "Heading: <input maxlength=\"50\" type=\"text\" value=\"${heading}\" name=\"heading\" id=\"heading\"> <br> "
          "<h2>Admin</h2> "
          "Client Timeout: <input type=\"number\" value=${timeout} name=\"timeout\" id=\"timeout\"> <br> "
          "Admin Password: <input maxlength=\"12\" type=\"text\" value=\"${adminpwd}\" name=\"adminpwd\" id=\"adminpwd\"> <br> "
          "<br> "
          "<input type=\"submit\"> <a href='/'><h4>Home Page</h4></a>"
        "</form>"
      ;

      content.replace("${ssid}", settings.ssid);
      content.replace("${pwd}", settings.pwd);
      content.replace("${title}", settings.title);
      content.replace("${heading}", settings.heading);
      content.replace("${timeout}", String(settings.timeout));
      content.replace("${adminpwd}", settings.adminPwd);
    }
  }

  // Send HTML Header...
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(""); //  do not forget this one
  
  // Send the page content...
  client.println(htmlPageTemplate(settings.title, "Device Settings", content));
}

/**
 * Verifies the updated setttings string to help ensure it is properly
 * formatted and everything is in the correct order.
 */
bool verifyNewSettings(String updateString, int map[5]) {
  if (updateString.length() > 0) { // There is something...
    for (int i = 0; i < 5; i++) { // Verify data in map...
      if (map[i] != -1 && (i == 0 || (i > 0 && map[i] > map[i - 1]))) { // keywords are found and in correct order...
        // So far so good, Carry on...
      } else return false;
    }

    if (map[1] - map[0] - 5 > 32) return false; // SSID cannot be more than 32 chars...
    if (map[2] - map[1] - 5 > 63) return false; // PWD cannot be more than 63 chars...
    if (map[3] - map[2] - 7 > 50) return false; // Title cannot be more than 50 chars...
    if (map[4] - map[3] - 9 > 50) return false; // Heading cannot be more than 50 chars...
    if (updateString.length() - map[5] - 10 > 12) return false; // Admin Pwd cannot be more than 12 characters...

    return true;
  }

  return false;
}

/************************************************************
 * UPDATE PAGE
 * **********************************************************
 * Used to handle update requests as well as show a page that 
 * indicates the results of the requested updates.
 */
void showAndHandleUpdatePage(WiFiClient client, String newSettings) {
  newSettings.trim();

  int sMap[5] = {0};
  sMap[0] = newSettings.indexOf("ssid=");
  sMap[1] = newSettings.indexOf("&pwd=");
  sMap[2] = newSettings.indexOf("&title=");
  sMap[3] = newSettings.indexOf("&heading=");
  sMap[4] = newSettings.indexOf("&timeout=");
  sMap[5] = newSettings.indexOf("&adminpwd=");

  bool needReboot = false;
  String content = "<h3>Given settings were not valid!!!</h3>";  
  if (verifyNewSettings(newSettings, sMap)) { // Settings shoudld be good...
    char ssid[32];
    strcpy(ssid, newSettings.substring(sMap[0] + 5, sMap[1]).c_str());
    char pwd[63];
    strcpy(pwd, newSettings.substring(sMap[1] + 5, sMap[2]).c_str());
    if (
      settings.factorySettings || // Factory Settings are being changed...
      strcmp(settings.ssid, ssid) != 0 || // SSID is being changed...
      strcmp(settings.pwd, pwd) != 0 // PWD is being changed...
    ) {
      needReboot = true;
    }
    strcpy(settings.ssid, ssid);
    strcpy(settings.pwd, pwd);
    strcpy(settings.title, newSettings.substring(sMap[2] + 7, sMap[3]).c_str());
    strcpy(settings.heading, newSettings.substring(sMap[3] + 9, sMap[4]).c_str());
    settings.timeout = newSettings.substring(sMap[4] + 9, sMap[5]).toInt();
    settings.factorySettings = false;
    strcpy(settings.adminPwd, newSettings.substring(sMap[5] + 10).c_str());

    if (persistSettings()) { // Successful...
      if (needReboot) { // Needs to reboot...
        content = "<h3>Settings update Successful!</h3>"
          "<h4>Device will reboot now...</h4>";
      } else { // No reboot needed; Send to home page...
        content = "<h3>Settings update Successful!</h3>"
          "<a href='/'><h4>Home Page</h4></a>";
      }
    } else { // Error...
      content = "<h3>Error Saving Settings!!!</h3>";
    }
  }

  // Send HTML Header...
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(""); //  do not forget this one

  client.println(htmlPageTemplate(settings.title, "Update Result", content));
  client.flush();
  delay(5000);

  if (needReboot) { // A reboot is required for requested changes...
    ESP.restart();
  }
}

/**********************************************************************
 * HTML PAGE TEMPLATE
 * ********************************************************************
 * This function is used to Generate the HTML for a web page where the 
 * title, heading and content is provided to the function as a String 
 * type.
 */
String htmlPageTemplate(String title, String heading, String content) {
  String result = ""
    "<!DOCTYPE HTML> "
    "<html lang=\"en\"> "
      "<head> "
        "<title>${title}</title> "
        "<style> "
          "body { background-color: #FFFFFF; color: #000000; } "
          "h1 { text-align: center; background-color: #5878B0; color: #FFFFFF; border: 3px; border-radius: 15px; } "
          "h2 { text-align: center; background-color: #58ADB0; color: #FFFFFF; border: 3px; } "
          "#wrapper { background-color: #E6EFFF; padding: 20px; margin-left: auto; margin-right: auto; max-width: 700px; box-shadow: 3px 3px 3px #333 } "
          "#info { font-size: 30px; font-weight: bold; line-height: 150%; } "
        "</style> "
      "</head> "
      ""
      "<div id=\"wrapper\"> "
        "<h1>${heading}</h1> "
        "<div id=\"info\">${content}</div> "
      "</div> "
    "</html>";

  result.replace("${title}", title);
  result.replace("${heading}", heading);
  result.replace("${content}", content);

  return result;
}

/**
 * Function handles flashing of the LED for signaling the given IP Address
 * entirely or simply its last octet as determined by the passed boolean 
 * refered to as quick. If quick is TRUE then last Octet is signaled, if 
 * FALSE then entire IP is signaled.
 */
void signalIpAddress(String ipAddress, bool quick) {
  if (!quick) { // Whole IP Requested...
    int octet[3];
    
    int index = ipAddress.indexOf('.');
    int index2 = ipAddress.indexOf('.', index + 1);
    int index3 = ipAddress.lastIndexOf('.');
    octet[0] = ipAddress.substring(0, index).toInt();
    octet[1] = ipAddress.substring(index + 1, index2).toInt();
    octet[2] = ipAddress.substring(index2 + 1, index3).toInt();

    for (int i = 0; i < 3; i++) { // Iterate first 3 octets and signal...
      displayOctet(octet[i]);
      displayNextOctetIndicator();
    }
  }

  // Signals 4th octet regardless of quick or not
  int fourth = ipAddress.substring(ipAddress.lastIndexOf('.') + 1).toInt();
  displayOctet(fourth);
  displayDone(); // Fast blink...
} 

/**
 * This function is in charge of displaying or signaling a single
 * octet of an IP address. 
 */
void displayOctet(int octet) {
  if (displayDigit(octet / 100)) {
    displayNextDigitIndicator();
    octet = octet % 100;
  }
  if (displayDigit(octet / 10)) {
    displayNextDigitIndicator();
  }
  displayDigit(octet % 10);
}

/**
 * This function's job is to generate flashes for 
 * a single digit.
 */
bool displayDigit(int digit) {
  digitalWrite(LED_PIN, HIGH); // off
  bool result = false; // Indicates a non-zero value if true.
  for (int i = 0; i < digit; i++) { // Once per value of the digit...
    result = true;
    digitalWrite(LED_PIN, LOW);
    delay(500);
    digitalWrite(LED_PIN, HIGH);
    delay(500);
  }

  return result;
}

/**
 * Displays or signals the separator between octets which
 * is simply 2 Next Digit Indicators.
 */
void displayNextOctetIndicator() {
  displayNextDigitIndicator();
  displayNextDigitIndicator();
}

/*
 * This displays the Next Digit Indicator which is simply a way to visually
 * break up digit flashes.
 */
void displayNextDigitIndicator() {
  digitalWrite(LED_PIN, HIGH);
  delay(700);
  for (int i = 0; i < 3; i++) { // Flash 3 times...
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
  }
  delay(900);
}

/*
 * This displays or signals the Display Done flashes.
 * This is a visual way for the Device to say it is done 
 * signaling the requested IP Address or last Octet.
 */
void displayDone() {
  digitalWrite(LED_PIN, HIGH); // Start off
  delay(1000);
  for (int i = 0; i < 20; i++) { // Do 20 flashes...
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
  }
}