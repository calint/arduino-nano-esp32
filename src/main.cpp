#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

// define WiFi login info 'secret_wifi_network' and 'secret_wifi_password'
#include "secrets.h"

constexpr const char *url_time_server = "http://worldtimeapi.org/api/ip";
constexpr const char *url_astros = "http://api.open-notify.org/astros.json";
constexpr const char *url_jokes = "https://v2.jokeapi.dev/joke/Programming";

// task 1 on second core
TaskHandle_t task1;
auto loop1() -> void;
auto func1(void *vpParameter) -> void {
  while (true) {
    loop1();
  }
}

// task 2 on second core
TaskHandle_t task2;
auto loop2() -> void;
auto func2(void *vpParameter) -> void {
  while (true) {
    loop2();
  }
}

WiFiServer web_server(80);

auto lookup_wifi_status_to_cstr(wl_status_t const status) -> const char * {
  switch (status) {
  case WL_CONNECTED:
    return "connected";
  case WL_NO_SHIELD:
    return "no shield";
  case WL_IDLE_STATUS:
    return "idle";
  case WL_NO_SSID_AVAIL:
    return "no wifi network available";
  case WL_SCAN_COMPLETED:
    return "scan completed";
  case WL_CONNECT_FAILED:
    return "connect failed";
  case WL_CONNECTION_LOST:
    return "connection lost";
  case WL_DISCONNECTED:
    return "disconnected";
  default:
    return "unknown";
  }
}

auto connect_to_wifi_if_disconnected() -> bool {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("\nconnecting to '%s' with '%s'\n", secret_wifi_network,
                secret_wifi_password);

  WiFi.mode(WIFI_STA);
  WiFi.begin(secret_wifi_network, secret_wifi_password);
  for (auto sts = WiFi.status(); sts != WL_CONNECTED; sts = WiFi.status()) {
    switch (sts) {
    case WL_CONNECT_FAILED:
      Serial.println("\n!!! connection to wifi failed");
      return false;
    case WL_NO_SSID_AVAIL:
      Serial.println("\n!!! network not found or wrong password");
      return false;
    default:
      break;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nconnected\nip: ");
  Serial.println(WiFi.localIP().toString());
  Serial.print("signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("auto-reconnect: ");
  Serial.println(WiFi.getAutoReconnect() ? "yes" : "no");
  return true;
}

// setup first core
auto setup() -> void {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);
  // wait max 10 seconds for serial over usb
  while (!Serial && millis() < 10000) {
    delay(100);
  }

  connect_to_wifi_if_disconnected();

  digitalWrite(LED_BUILTIN, HIGH);

  Preferences prefs;
  prefs.begin("store");
  auto const boot_count = prefs.getUInt("boot_count", 0) + 1;
  Serial.print("boot count: ");
  Serial.println(boot_count);
  prefs.putUInt("boot_count", boot_count);
  prefs.end();

  // setup second core
  web_server.begin();

  Serial.print("FreeRTOS kernel version: ");
  Serial.println(tskKERNEL_VERSION_NUMBER);
  // start second core
  Serial.println("starting task1 on core1");
  auto const res1 = xTaskCreatePinnedToCore(
      func1, "core1-task1", 16 * 1024, NULL, 1, &task1, !ARDUINO_RUNNING_CORE);
  if (res1 != pdPASS) {
    Serial.print("!!! error: ");
    Serial.println(res1);
    while (true)
      ;
  }
  Serial.println("starting task2 on core1");
  auto const res2 = xTaskCreatePinnedToCore(
      func2, "core1-task2", 1 * 1024, NULL, 2, &task2, !ARDUINO_RUNNING_CORE);
  if (res2 != pdPASS) {
    Serial.print("!!! error: ");
    Serial.println(res2);
    while (true)
      ;
  }
}

// returns true if request succeeded or false if something went wrong
auto read_url_to_json_doc(const char *url, JsonDocument &json_doc) -> bool {
  HTTPClient http_client;
  http_client.useHTTP10();
  http_client.setConnectTimeout(10000);
  http_client.setTimeout(10000);

  if (!http_client.begin(url)) {
    Serial.printf("!!! unable to connect to %s\n", url);
    return false;
  }
  auto const http_code = http_client.GET();
  if (http_code != HTTP_CODE_OK) {
    Serial.print("!!! GET error: ");
    Serial.print(http_code);
    Serial.print(": ");
    Serial.println(http_client.errorToString(http_code));
    http_client.end();
    return false;
  }
  auto const json_error = deserializeJson(json_doc, http_client.getStream());
  http_client.end();
  if (json_error) {
    Serial.printf("!!! json parsing failed: %s\n", json_error.c_str());
    return false;
  }
  return true;
}

auto print_astronauts_in_space_right_now(Stream &os) -> void {
  digitalWrite(LED_BUILTIN, LOW);
  JsonDocument json_doc;
  if (!read_url_to_json_doc(url_astros, json_doc)) {
    return;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  auto const people = json_doc["people"].as<JsonArrayConst>();
  for (auto const p : people) {
    // note. not "auto const&" due to example at:
    //       https://arduinojson.org/v6/api/jsonarray/begin_end/
    os.println(p["name"].as<const char *>());
  }
}

auto print_current_time_based_on_ip(Stream &os) -> void {
  digitalWrite(LED_BUILTIN, LOW);
  JsonDocument json_doc;
  if (!read_url_to_json_doc(url_time_server, json_doc)) {
    return;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  auto const date_time_raw = json_doc["datetime"].as<String>();
  //  "2023-08-31T16:32:47.653086+02:00" to "2023-08-31 16:32:47"
  auto const date_time =
      date_time_raw.substring(0, 10) + " " + date_time_raw.substring(11, 19);
  os.println(date_time);
}

auto print_random_programming_joke(Stream &os) -> void {
  digitalWrite(LED_BUILTIN, LOW);
  JsonDocument json_doc;
  if (!read_url_to_json_doc(url_jokes, json_doc)) {
    return;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  if (json_doc["type"].as<String>() == "single") {
    os.println(json_doc["joke"].as<const char *>());
  } else {
    os.println(json_doc["setup"].as<const char *>());
    os.println(json_doc["delivery"].as<const char *>());
  }
}

auto print_current_time_from_ntp(Stream &os) -> void {
  WiFiUDP ntp_udp;
  // default 'pool.ntp.org', 60 seconds update interval, no offset
  NTPClient ntp_client(ntp_udp);
  digitalWrite(LED_BUILTIN, LOW);
  if (!ntp_client.update()) {
    os.println("!!! failed to update ntp client");
  }
  digitalWrite(LED_BUILTIN, HIGH);
  os.println(ntp_client.getFormattedTime());
}

auto print_web_server_ip(Stream &os) -> void {
  os.println(WiFi.localIP().toString());
}

auto print_wifi_status(Stream &os) -> void {
  os.print(lookup_wifi_status_to_cstr(WiFi.status()));
  os.print(" ");
  os.print(WiFi.RSSI());
  os.println(" dBm");
}

auto print_heap_info(Stream &os) -> void {
  os.print("used: ");
  os.print(ESP.getHeapSize() - ESP.getFreeHeap());
  os.println(" B");
  os.print("free: ");
  os.print(ESP.getFreeHeap());
  os.println(" B");
  os.print("total: ");
  os.print(ESP.getHeapSize());
  os.println(" B");
}

auto print_boot_count(Stream &os) -> void {
  Preferences prefs;
  prefs.begin("store", true);
  os.print("boot count: ");
  os.println(prefs.getUInt("boot_count", 0));
  prefs.end();
}

auto print_stack_info(Stream &os) -> void {
  os.print("lowest free: ");
  os.print(uxTaskGetStackHighWaterMark(NULL));
  os.println(" B");
}

auto print_output_to_stream(Stream &os) -> void {
  os.println("\ncurrent time based on ip:");
  print_current_time_based_on_ip(os);

  os.println("\ncurrent time in utc from ntp:");
  print_current_time_from_ntp(os);

  os.println("\nastronauts in space right now:");
  print_astronauts_in_space_right_now(os);

  os.println("\nprogramming joke:");
  print_random_programming_joke(os);

  os.println("\nweb server ip:");
  print_web_server_ip(os);

  os.println("\nwifi status: ");
  print_wifi_status(os);

  os.println("\nstored in flash:");
  print_boot_count(os);

  os.println("\nheap info:");
  print_heap_info(os);

  os.println("\nstack info:");
  print_stack_info(os);
}

// serve "/"
auto handle_web_server_root(String const &path, String const &query,
                            std::vector<String> const &headers, Stream &os)
    -> void {
  os.print("<pre>path: ");
  os.println(path);
  os.print("query: ");
  os.println(query);
  for (auto const &s : headers) {
    os.println(s);
  }

  print_heap_info(os);
}

// serve "/status"
auto handle_web_server_status(String const &path, String const &query,
                              std::vector<String> const &headers, Stream &os)
    -> void {
  os.print("<pre>path: ");
  os.println(path);
  os.print("query: ");
  os.println(query);
  for (auto const &s : headers) {
    os.println(s);
  }

  print_output_to_stream(os);
}

// serve "/rgbled"
auto handle_web_server_rgbled(String const &path, String const &query,
                              std::vector<String> const &headers, Stream &os)
    -> void {
  auto const r = query.indexOf("r=1") != -1;
  auto const g = query.indexOf("g=1") != -1;
  auto const b = query.indexOf("b=1") != -1;

  digitalWrite(LED_RED, r ? LOW : HIGH);
  digitalWrite(LED_GREEN, g ? LOW : HIGH);
  digitalWrite(LED_BLUE, b ? LOW : HIGH);

  os.println("<!doctype html><meta name=viewport "
             "content=\"width=device-width,initial-scale=1\"><meta "
             "charset=utf-8><style>*{font-family:monospace}</style><title>RGB "
             "Led</title>");
  os.print("<form>RGB Led: ");

  os.print("<input type=checkbox name=r value=1 ");
  if (r) {
    os.print("checked");
  }
  os.print("> red ");

  os.print("<input type=checkbox name=g value=1 ");
  if (g) {
    os.print("checked");
  }
  os.print("> green ");

  os.print("<input type=checkbox name=b value=1 ");
  if (b) {
    os.print("checked");
  }
  os.print("> blue ");

  os.print("<input type=submit value=apply>");
  os.print("</form>");
}

// returns true if a request was serviced or false if no client available
auto handle_web_server() -> bool {
  auto client = web_server.available();
  if (!client) {
    return false;
  }
  // read first request line
  auto const method = client.readStringUntil(' ');
  auto const uri = client.readStringUntil(' ');
  auto const version = client.readStringUntil('\r');
  if (client.read() != '\n') {
    Serial.println("!!! malformed http request");
    return false;
  }

  auto const query_start_ix = uri.indexOf("?");
  auto const path =
      query_start_ix == -1 ? uri : uri.substring(0, query_start_ix);
  auto const query =
      query_start_ix == -1 ? "" : uri.substring(query_start_ix + 1);

  std::vector<String> headers;
  // var nheaders = 32;  // maximum number of headers
  // while (nheaders--) {
  while (true) {
    auto const line = client.readStringUntil('\r');
    if (client.read() != '\n') {
      Serial.println("!!! malformed http request");
      return false;
    }
    if (line.length() == 0) {
      break;
    }
    headers.push_back(line);
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println();

  if (path == "/") {
    handle_web_server_root(path, query, headers, client);
  } else if (path == "/status") {
    handle_web_server_status(path, query, headers, client);
  } else if (path == "/rgbled") {
    handle_web_server_rgbled(path, query, headers, client);
  } else {
    client.print("unknown path '");
    client.print(path);
    client.println("'");
  }

  client.stop();

  return true;
}

// loop on first core
auto loop() -> void {
  if (connect_to_wifi_if_disconnected()) {
    print_output_to_stream(Serial);
  }
  delay(10000);
}

// task 1 on second core
auto loop1() -> void {
  while (handle_web_server())
    ;
  delay(100); // slightly less busy wait
}

// task 2 on second core
auto loop2() -> void {
  digitalWrite(LED_BLUE, HIGH);
  delay(500);
  digitalWrite(LED_BLUE, LOW);
  delay(500);
}
