// Written for Grafana Labs to demonstrate how to use the M5Stick CPlus2 with Grafana Cloud
// 2024/02/10
// Willie Engelbrecht - willie.engelbrecht@grafana.com
// Introduction to time series: https://grafana.com/docs/grafana/latest/fundamentals/timeseries/
// M5StickCPlus2: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
// Register for a free Grafana Cloud account including free metrics and logs: https://grafana.com

#include "M5StickCPlus2.h"
#include "M5UnitENV.h"

// ===================================================
// All the things that needs to be changed 
// Your local WiFi details
// Your Grafana Cloud details
// ===================================================
#include "config.h"

// ===================================================
// Includes - Needed to write Prometheus or Loki metrics/logs to Grafana Cloud
// No need to change anything here
// ===================================================
#include "certificates.h"
#include <PromLokiTransport.h>
#include <PrometheusArduino.h>

// ===================================================
// Global Variables
// ===================================================
SHT3X sht30;              // temperature and humidity sensor
QMP6988 qmp6988;          // pressure sensor

float temp     = 0.0;
float hum      = 0.0;
float pressure = 0.0;

int upload_fail_count = 0;

// Client for Prometheus metrics
PromLokiTransport transport;
PromClient client(transport);

// Create a write request for 11 time series.
WriteRequest req(11, 2048);

// Define all our timeseries
TimeSeries ts_m5stick_temperature(1, "m5stick_temp", PROM_LABELS);
TimeSeries ts_m5stick_humidity(1, "m5stick_hum", PROM_LABELS);
TimeSeries ts_m5stick_pressure(1, "m5stick_pressure", PROM_LABELS);
TimeSeries ts_m5stick_bat_volt(1, "m5stick_bat_volt", PROM_LABELS);
TimeSeries ts_m5stick_bat_current(1, "m5stick_bat_current", PROM_LABELS);
TimeSeries ts_m5stick_bat_level(1, "m5stick_bat_level", PROM_LABELS);

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Display.setRotation(3);
    
    //StickCP2.Display.setTextDatum(middle_center);
    //StickCP2.Display.setTextFont(&fonts::Orbitron_Light_24);
    
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.setTextColor(ORANGE, BLACK);
    StickCP2.Display.printf("== Grafana Labs ==");
    StickCP2.Display.setTextColor(WHITE, BLACK);

    Wire.begin();       // Wire init, adding the I2C bus.  
    if (!qmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 32, 33, 400000U)) {
        Serial.println("Couldn't find QMP6988");
    }
    if (!sht30.begin(&Wire, SHT3X_I2C_ADDR, 32, 33, 400000U)) {
        Serial.println("Couldn't find SHT3X");
    }

    StickCP2.Display.setCursor(10, 30);
    StickCP2.Display.printf("Hello, %s", YOUR_NAME);
    StickCP2.Display.setCursor(10, 60);
    StickCP2.Display.printf("Please wait:\r\n Connecting to WiFi");

    // Connecting to Wifi
    transport.setUseTls(true);
    transport.setCerts(grafanaCert, strlen(grafanaCert));
    transport.setWifiSsid(WIFI_SSID);
    transport.setWifiPass(WIFI_PASSWORD);
    transport.setDebug(Serial);  // Remove this line to disable debug logging of the client.
    if (!transport.begin()) {
        Serial.println(transport.errmsg);
        while (true) {};        
    }

    StickCP2.Display.setCursor(10, 105);
    StickCP2.Display.setTextColor(GREEN, BLACK);
    StickCP2.Display.printf("Connected!");
    delay(1500); 

    // Configure the Grafana Cloud client
    client.setUrl(GC_URL);
    client.setPath((char*)GC_PATH);
    client.setPort(GC_PORT);
    client.setUser(GC_USER);
    client.setPass(GC_PASS);
    client.setDebug(Serial);  // Remove this line to disable debug logging of the client.
    if (!client.begin()) {
        Serial.println(client.errmsg);
        while (true) {};
    }

    // Add our TimeSeries to the WriteRequest
    req.addTimeSeries(ts_m5stick_temperature);
    req.addTimeSeries(ts_m5stick_humidity);
    req.addTimeSeries(ts_m5stick_pressure);
    req.addTimeSeries(ts_m5stick_bat_volt);
    req.addTimeSeries(ts_m5stick_bat_current);
    req.addTimeSeries(ts_m5stick_bat_level);
}

void loop() {
    int64_t time;
    time = transport.getTimeMillis();
    Serial.printf("\r\n====================================\r\n");

    // Get new updated values from our sensor
    if (qmp6988.update()) {
        pressure = qmp6988.calcPressure();
    }
    if (sht30.update()) {     // Obtain the data of sht30.
        temp = sht30.cTemp;      // Store the temperature obtained from sht30.
        hum  = sht30.humidity;   // Store the humidity obtained from the sht30.
    } else {
        temp = 0, hum = 0;
    }    
    if (pressure < 950) { ESP.restart(); } // Sometimes this sensor fails, and if we get an invalid reading it's best to just restart the controller to clear it out
    if (pressure/100 > 1200) { ESP.restart(); } // Sometimes this sensor fails, and if we get an invalid reading it's best to just restart the controller to clear it out
    Serial.printf("Temp: %2.1f °C \r\nHumidity: %2.0f%%  \r\nPressure:%2.0f hPa\r\n\n", temp, hum, pressure / 100);

    // Gather some internal data as well, about battery states, voltages, charge rates and so on
    int bat_volt = StickCP2.Power.getBatteryVoltage();
    Serial.printf("Battery Volt: %dmv \n", bat_volt);

    int bat_current = StickCP2.Power.getBatteryCurrent();
    Serial.printf("Battery Current: %dmv \n", bat_current);

    int bat_level = StickCP2.Power.getBatteryLevel();
    Serial.printf("Battery Level: %d% \n\n", bat_level);

    // Now add all of our collected data to the timeseries
    ts_m5stick_temperature.addSample(time, temp);
    ts_m5stick_humidity.addSample(time, hum);
    ts_m5stick_pressure.addSample(time, pressure);

    ts_m5stick_bat_volt.addSample(time, bat_volt);
    ts_m5stick_bat_current.addSample(time, bat_current);
    ts_m5stick_bat_level.addSample(time, bat_level);

    // Now send all of our data to Grafana Cloud!
    PromClient::SendResult res = client.send(req);
    ts_m5stick_temperature.resetSamples();
    ts_m5stick_humidity.resetSamples();
    ts_m5stick_pressure.resetSamples();
    ts_m5stick_bat_volt.resetSamples();
    ts_m5stick_bat_current.resetSamples();
    ts_m5stick_bat_level.resetSamples();

    StickCP2.Display.clear();  
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 10);
    StickCP2.Display.setTextColor(ORANGE, BLACK);
    StickCP2.Display.printf("== Grafana Labs ==");
    StickCP2.Display.setTextColor(WHITE, BLACK);
    //StickCP2.Display.setTextSize(1);    
    StickCP2.Display.setCursor(0, 40);
    StickCP2.Display.printf("  Temp: %2.1f  \r\n  Humi: %2.0f%%  \r\n  Pressure:%2.0f hPa\r\n", temp, hum, pressure / 100);

    // Display some debug information on the screen to make it easier to determine if your device is working or not. Can be disabled in the config.h file
    if (LCD_SHOW_DEBUG_INFO == "1") {
      // Are we connected to WiFi or not ?
      StickCP2.Display.setCursor(0, 92);
      if (WiFi.status() != WL_CONNECTED) {
        StickCP2.Display.setTextColor(WHITE, BLACK);
        StickCP2.Display.printf("  Wifi: ");
        StickCP2.Display.setTextColor(RED, BLACK);
        StickCP2.Display.printf("Not connected!");
      }
      else {          
        StickCP2.Display.printf("  Wifi: ");
        StickCP2.Display.setTextColor(GREEN, BLACK);
        StickCP2.Display.printf("Connected");
      }

      // Are we able to upload metrics or not - display on the LCD screen if configured in config.h
      StickCP2.Display.setCursor(0, 110);
      if (res == 0) {
        upload_fail_count = 0;
        StickCP2.Display.setTextColor(WHITE, BLACK);
        StickCP2.Display.printf("  Upload ");
        StickCP2.Display.setTextColor(GREEN, BLACK);
        StickCP2.Display.printf("complete");
      } else {
        upload_fail_count += 1;
        StickCP2.Display.setTextColor(WHITE, BLACK);
        StickCP2.Display.printf("  Upload ");
        StickCP2.Display.setTextColor(RED, BLACK);
        StickCP2.Display.printf("failed: %s", String(upload_fail_count));
      }
    }

    delay(5000);
}
