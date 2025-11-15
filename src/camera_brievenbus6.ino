// mailbox detector
// Jef Collin 2024
// V 1.0


// todo


#include "esp_camera.h"
#include <WiFi.h>
#include "driver/rtc_io.h"
#include <ESP_Mail_Client.h>

boolean debugloop = false;

// wifi parameters
const char* ssid = "your_ssid";
const char* password = "your_password";

// SMTP parameters
#define AUTHOR_EMAIL    "xxx@gmail.com"
#define AUTHOR_PASSWORD "replace! make in gmail authenticiation"
#define SMTP_HOST        "smtp.gmail.com"
#define SMTP_PORT        465
#define RECIPIENT_EMAIL  "xxx@gmail.com"

#define EmailSubject     "My letterbox"

// cap on messages per day
#define MaximumMessagesPerDay 10

#define CAMERA_MODEL_AI_THINKER // Has PSRAM
#include <camera_pins.h>

// echo from sensor
#define Pin_Echo 3
// trigger for sensor
#define Pin_Trigger 1
// doorcontact mailbox
#define Pin_Door_Letter 13
// doorcontact packages
#define Pin_Door_Box 14
// LED strip
#define Pin_LEDS 4

// global buffer to duplicate picture buffer into so original buffer can be freed
uint8_t *PictureBuffer = NULL;
size_t PictureBufferLen = 0;

// distance measured
long EchoDistance = 0;

// trigger threshold in cm, modify as needed
int EchoTriggerDistance = 25;

// sensor triggered
boolean EchoTriggered = false;

// ignore echo sensor trigger if mailbox door is open
boolean EchoIgnoreTrigger = false;

// change status for echo and switches
boolean EchoStatus = false;
boolean PreviousEchoStatus = false;

byte LetterDoorStatus = 0;
byte PreviousLetterDoorStatus = 2;

byte BoxDoorStatus = 0;
byte PreviousBoxDoorStatus = 2;

// letter door open
boolean LetterDoorTriggered = false;

// if warning for open letter door is sent
boolean LetterDoorStillOpenWarningSent = false;

// box door open
boolean BoxDoorTriggered = false;

// if warning for open box door is sent
boolean BoxDoorStillOpenWarningSent = false;

// ignore box door if opened by me, open letter door first
boolean IgnoreBoxDoorTrigger = false;

// mail is stuck in opening
boolean MailStuck = false;

// LEDs status
boolean LEDIsOn = false;

// count number of emails, limit mails per day
int EmailCounter = 0;

// timer for sensors
unsigned long TimerEcho  = millis();
unsigned long TimerLetterDoor  = millis();
unsigned long TimerBoxDoor  = millis();

// timer for LEDs
unsigned long TimerLED  = millis();

// timer for mails per day
unsigned long TimerEmailLimit  = millis();

// minus 10 minutes to ensure mail is detected within time limit
unsigned long LastTrigger  = millis() - 600000;

// global used SMTPSession object for SMTP transport
SMTPSession smtp;

// global used Session_Config for user defined session credentials
Session_Config config;

/* callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

void setup() {
  if (debugloop) {
    Serial.begin(115200);
  }

  // configure inputs and outputs
  if (!debugloop) {
    pinMode(Pin_Trigger, OUTPUT);
    pinMode(Pin_Echo, INPUT);
    pinMode(Pin_Door_Letter, INPUT_PULLUP);
    pinMode(Pin_Door_Box, INPUT_PULLUP);
  }

  pinMode(Pin_LEDS, OUTPUT);
  digitalWrite(Pin_LEDS, LOW);

  // allow pins to settle
  delay(200);

  // make sure wifi is disabled
  WiFi.disconnect(true);

  WiFi.mode(WIFI_OFF);

  if (!debugloop) {
    // first measurement for power on mail
    EchoDistance = MeasureDistance();
  }

  //  network reconnection option
  MailClient.networkReconnect(true);

  // callback function to get the sending results
  smtp.callback(smtpCallback);

  if (debugloop) {
    //   smtp.debug(1);
  }

  // send startup mail
  SendMail(4, true);

  delay(5000);

}

void loop() {

  if (!debugloop) {

    // get door switches
    LetterDoorStatus = digitalRead(Pin_Door_Letter);
    BoxDoorStatus = digitalRead(Pin_Door_Box);

    // letter door flow
    if (LetterDoorStatus != PreviousLetterDoorStatus) {
      // letter door open
      if (LetterDoorStatus == 1) {
        // door opened
        // to avoid mails if we open the box door ignore triggers when letter door is open
        IgnoreBoxDoorTrigger = true;
        // ignore echo triggers if letter door open in case we need to pull out mail from the slot or push stuck mail in
        EchoIgnoreTrigger = true;
        // clear triggered
        EchoTriggered = false;
        // set trigger flag
        LetterDoorTriggered = true;
        // allow new mails
        EmailCounter = 0;
        // reset timer
        TimerEmailLimit = millis();
        // start timer
        TimerLetterDoor  = millis();
        // turn led on
        digitalWrite(Pin_LEDS, HIGH);
        LEDIsOn = true;
        // start timer
        TimerLED  = millis();
      }
      else
      {
        // reset, allow mails for box door
        IgnoreBoxDoorTrigger = false;
        // reset, arm echo sensor
        EchoIgnoreTrigger = false;
        // turn led off if still on
        if (LEDIsOn) {
          digitalWrite(Pin_LEDS, LOW);
          LEDIsOn = false;
        }
        LetterDoorTriggered = false;
        LetterDoorStillOpenWarningSent = false;
      }
      PreviousLetterDoorStatus = LetterDoorStatus;
    }

    // send warning once if still open after a while
    if (LetterDoorTriggered and millis() - TimerLetterDoor >= 300000 and !LetterDoorStillOpenWarningSent) {
      // door still open
      SendMail(3, false);
      // set flag to prevent from sending warning again
      LetterDoorStillOpenWarningSent = true;
    }

    // catch led on for a long time
    if (LEDIsOn and millis() - TimerLED >= 240000) {
      digitalWrite(Pin_LEDS, LOW);
      LEDIsOn = false;
    }

    // box door flow
    if (BoxDoorStatus != PreviousBoxDoorStatus) {
      // box door open
      if (BoxDoorStatus == 1) {
        // ignore box door if letter door is open
        if (!IgnoreBoxDoorTrigger) {
          // door opened
          SendMail(1, false);
          // set triggered
          BoxDoorTriggered = true;
          // start timer
          TimerBoxDoor = millis();
        }
      }
      else {
        // reset triggered
        BoxDoorTriggered = false;
        BoxDoorStillOpenWarningSent = false;
      }
      // keep current switch
      PreviousBoxDoorStatus = BoxDoorStatus;
    }

    // send warning once if still open after a while
    if (BoxDoorTriggered and millis() - TimerBoxDoor >= 1800000 and !BoxDoorStillOpenWarningSent) {
      // door still open
      SendMail(2, false);
      // set flag to prevent from sending warning again
      BoxDoorStillOpenWarningSent = true;
    }

    // echo flow
    if (!EchoIgnoreTrigger) {
      EchoDistance = MeasureDistance();
      EchoStatus = (EchoDistance <= EchoTriggerDistance);
      if (EchoStatus != PreviousEchoStatus) {
        // below trigger distance
        if (EchoStatus) {
          // already triggered
          if (EchoTriggered) {
            // check for stuck mail or 1 day timeout
            if ((millis() - TimerEcho >= 300000 and !MailStuck) or (millis() - TimerEcho >= 86400000 and MailStuck)) {
              SendMail(5, true);
              MailStuck = true;
              // reset timer
              TimerEcho = millis();
            }
          }
          else {
            // if not triggered yet send mail
            // ignore if less than 4 minutes after previous detection
            if (millis() - LastTrigger >= 240000) {
              SendMail(0, true);
              // set triggered
              EchoTriggered = true;
              // start timer
              TimerEcho = millis();
              // keep last trigger time
              LastTrigger = millis();
            }
          }
        }
        else {
          // clear triggered
          EchoTriggered = false;
          // reset post in klep
          MailStuck = false;
        }
        PreviousEchoStatus = EchoStatus;
      }
    }

  }
  else {
    // in test mode just take a picture and email every 5 seconds
    SendMail(0, true);

    delay(5000);
  }


  // time limit for emails per day
  if (millis() - TimerEmailLimit > 86400000) {
    // reset email counter allowing new emails
    EmailCounter = 0;
    // reset timer
    TimerEmailLimit = millis();
  }

  // loop delay
  delay(50);
}


// measure distance
long MeasureDistance(void) {
  long duration;
  float distance;
  // set default to max distance in case of timeout
  long returndistance = 200;

  // sensor is triggered by a HIGH pulse of 10 or more microseconds.
  // short LOW pulse beforehand to ensure a clean HIGH pulse
  digitalWrite(Pin_Trigger, LOW);
  delayMicroseconds(5);
  digitalWrite(Pin_Trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(Pin_Trigger, LOW);

  // read the signal from the sensor: a HIGH pulse whose
  // duration is the time (in microseconds) from the sending
  // of the ping to the reception of its echo off of an object.
  // wait for max 12000 us = ca 200 cm
  duration = pulseIn(Pin_Echo, HIGH, 12000);

  if (duration > 0) {
    // convert the time into a distance in cm
    distance = (duration / 2) * 0.0343;
    // round
    returndistance = long(distance + 0.5);
    if (returndistance > 200) {
      returndistance = 200;
    }
  }
  return returndistance;
}


// send the mail, take picture is required
void SendMail(byte messagemode, boolean takepicture) {

  // check if limit is exceeded
  if (EmailCounter < MaximumMessagesPerDay) {

    String mailmessage;
    switch (messagemode) {
      case 0:
        // mail detected
        mailmessage = F("Post in de brievenbus.");
        break;

      case 1:
        // box door is opened
        mailmessage = F("Pakjesdeur geopend.");
        break;

      case 2:
        // box door is still open
        mailmessage = F("Pakjesdeur te lang open.");
        break;

      case 3:
        // letter door is still open
        mailmessage = F("Deur van brieven staat nog open.");
        break;

      case 4:
        // startup message
        mailmessage = F("Detector opgestart.");
        break;

      case 5:
        // mail stuck
        mailmessage = F("Post vast in de klep.");
        break;

    }

    // warning if nr of messages is reached
    if (EmailCounter == MaximumMessagesPerDay - 1) {
      mailmessage = mailmessage + "\nLimiet bereikt, geen verdere berichten voor vandaag!";
    }

    if (takepicture) {
      initializeCamera();
      // led on
      digitalWrite(Pin_LEDS, HIGH);
      delay(300);

      // capture the jpg image from camera
      camera_fb_t * fb = NULL;
      // take a number of pictures to allow camera to adjust
      for (byte x = 0; x < 15; x++) {
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb); // dispose the buffered image
        fb = NULL; // reset to capture errors
      }
      fb = esp_camera_fb_get(); // get fresh image

      // led off
      digitalWrite(Pin_LEDS, LOW);

      // allocate memory for the buffer
      PictureBufferLen = fb->len;
      PictureBuffer = (uint8_t *)malloc(PictureBufferLen);
      // copy the picture to the buffer
      memcpy(PictureBuffer, fb->buf, PictureBufferLen);

      // release the camera to reduce power before turning on wifi
      esp_camera_fb_return(fb);

      esp_camera_deinit();

      // power down the camera
      powerDownCamera();

    }

    // setup wifi
    WiFi.begin(ssid, password);

    WiFi.setSleep(false);

    int WiFiConnectRetries = 0;
    // limit retries to avoid hangups
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      WiFiConnectRetries++;
      if (WiFiConnectRetries > 360) {
        exit;
      }
    }
    if (WiFiConnectRetries > 360) {
      // limit reached, connection failed

    }
    else {
      // Serial.println(WiFi.localIP());

      config.server.host_name = SMTP_HOST;
      config.server.port = SMTP_PORT;
      config.login.email = AUTHOR_EMAIL;
      config.login.password = AUTHOR_PASSWORD;

      // For client identity, assign invalid string can cause server rejection
      config.login.user_domain = "gmail.com";

      //  /*
      //    Set the NTP config time
      //    For times east of the Prime Meridian use 0-12
      //    For times west of the Prime Meridian add 12 to the offset.
      //    Ex. American/Denver GMT would be -6. 6 + 12 = 18
      //    See https://en.wikipedia.org/wiki/Time_zone for a list of the GMT/UTC timezone offsets
      //  */
      config.time.ntp_server = "pool.ntp.org,time.nist.gov";
      config.time.gmt_offset = 1;
      config.time.day_light_offset = 0;

      // Declare the SMTP_Message class variable to handle to message being transport
      SMTP_Message message;

      /* Enable the chunked data transfer with pipelining for large message if server supported */
      message.enable.chunking = true;

      // message headers
      message.sender.name = AUTHOR_EMAIL;
      message.sender.email = AUTHOR_EMAIL;
      message.subject = EmailSubject;
      message.addRecipient("Jef", RECIPIENT_EMAIL);
      // message content

      // startup message, add wifi parameter
      if (messagemode == 4) {
        mailmessage = mailmessage + "\nWiFi signaal " + String(WiFi.RSSI()) + " dBm.";
      }

      message.text.content = mailmessage;

      message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

      message.html.charSet = F("utf-8");

      // attach if picture taken
      if (takepicture) {

        // Declare the attachment data
        SMTP_Attachment att;

        att.descr.filename = "mailbox.jpg";
        att.descr.mime = "image/jpg";

        att.blob.data = PictureBuffer;
        att.blob.size = PictureBufferLen;

        att.descr.content_id = F("image-001"); // The content id (cid) of camera.jpg image in the src tag

        att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;

        message.addAttachment(att);

      }

      //   Serial.println("Connection setup");

      boolean ConnectionOk = false;
      for (byte ConnectionRetry = 0; ConnectionRetry < 5; ConnectionRetry++ ) {
        /* connect to the server */
        if (smtp.connect(&config))
        {
          //    Serial.println("Connection ok");
          ConnectionOk = true;
          break;
        }
        else {
          //      Serial.println("Connection error-retry");
        }
      }

      if (ConnectionOk and smtp.isAuthenticated()) {
        // send the Email and close the session
        if (!MailClient.sendMail(&smtp, &message, true)) {
          //       Serial.println("Error authen");
        }
      }
    }

    // shutdown wifi
    WiFi.disconnect(true, true);

    WiFi.mode(WIFI_OFF);

    // count mails sent
    EmailCounter++;
  }
}

// setup camera
bool initializeCamera(void) {
  // ensure the camera is properly powered up
  powerUpCamera();
  delay(100); // ensure proper power up time

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // large size if psram is present, it is on esp32-cam
  config.frame_size = FRAMESIZE_UXGA;
  // Image sizes: 160x120 (QQVGA), 128x160 (QQVGA2), 176x144 (QCIF), 240x176 (HQVGA), 320x240 (QVGA),
  //              400x296 (CIF), 640x480 (VGA, default), 800x600 (SVGA), 1024x768 (XGA), 1280x1024 (SXGA),
  //              1600x1200 (UXGA)
  config.jpeg_quality = 10;                     // 0-63 lower number means higher quality (can cause failed image capture if set too low at higher resolutions)
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_count = 1;

  //  // Init with high specs to pre-allocate larger buffers
  //  if (psramFound()) {
  //    config.frame_size = FRAMESIZE_UXGA;
  //    config.jpeg_quality = 10;
  //    config.fb_count = 2;
  //  } else {
  //    config.frame_size = FRAMESIZE_SVGA;
  //    config.jpeg_quality = 12;
  //    config.fb_count = 1;
  //  }

  // deinitialize the camera before initializing it again
  esp_camera_deinit();

  // Camera init
  if (esp_camera_init(&config) != ESP_OK) {
    //   Serial.println("Camera init failed");
    return false;
  }

  cameraImageSettings();

  return true;
}

void powerDownCamera() {
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
}

void powerUpCamera() {
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(100); // Wait for camera to power up
}

// callback function to get the Email sending status
void smtpCallback(SMTP_Status status)
{
  //  /* Print the current status */
  //  Serial.println(status.info());

  /* Print the sending result */
  if (status.success())
  {
    // MailClient.printf used in the examples is for format printing via debug Serial port
    // that works for all supported Arduino platform SDKs e.g. SAMD, ESP32 and ESP8266.
    // In ESP8266 and ESP32, you can use Serial.printf directly.

    //    Serial.println("----------------");
    //    MailClient.printf("Message sent success: %d\n", status.completedCount());
    //    MailClient.printf("Message sent failed: %d\n", status.failedCount());
    //    Serial.println("----------------\n");

    //    for (size_t i = 0; i < smtp.sendingResult.size(); i++)
    //    {
    //      /* Get the result item */
    //      SMTP_Result result = smtp.sendingResult.getItem(i);
    //
    //      // In case, ESP32, ESP8266 and SAMD device, the Timer get from result.Timer should be valid if
    //      // your device time was synched with NTP server.
    //      // Other devices may show invalid Timer as the device time was not set i.e. it will show Jan 1, 1970.
    //      // You can call smtp.setSystemTime(xxx) to set device time manually. Where xxx is Timer (seconds since Jan 1, 1970)
    //
    //      MailClient.printf("Message No: %d\n", i + 1);
    //      MailClient.printf("Status: %s\n", result.completed ? "success" : "failed");
    //      MailClient.printf("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.Timer, "%B %d, %Y %H:%M:%S").c_str());
    //      MailClient.printf("Recipient: %s\n", result.recipients.c_str());
    //      MailClient.printf("Subject: %s\n", result.subject.c_str());
    //    }
    //    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  }
}


// set camera specific parameters
bool cameraImageSettings() {

  //    s->set_brightness(s, 1);               // I want image about 1 stop brighter (-2 to 2)
  //    s->set_contrast(s, 0);
  //    s->set_saturation(s, 0);
  //    s->set_gain_ctrl(s, 0);                // Auto gain off
  //    s->set_exposure_ctrl(s, 0);            // Auto exposure off
  //    s->set_aec_value(s, 100);              // Set aec to a small initial value.
  //    s->set_awb_gain(s, 1);                 // Auto White Balance enable (0 or 1)
  //    s->set_wb_mode(s, 0);                  // If awb_gain enabled (0-Auto,1-Sunny,2-Cloudy,3-Office,4-Home)
  //    s->set_aec2(s, 1);                     // AEC DSP
  //    s->set_bpc(s, 1);                      // Black point control
  //    s->set_wpc(s, 1);                      // White point control
  //    s->set_lenc(s, 1);                     // Lens correction
  //


  sensor_t *s = esp_camera_sensor_get();

  // enable auto adjust
  s->set_gain_ctrl(s, 1);                       // auto gain on
  s->set_exposure_ctrl(s, 1);                   // auto exposure on
  s->set_awb_gain(s, 1);                        // Auto White Balance enable (0 or 1)
  s->set_brightness(s, 0);  // (-2 to 2) - set brightness

  //  // Apply manual settings
  //  //  s->set_gain_ctrl(s, 0);                       // auto gain off
  //  //  s->set_awb_gain(s, 1);                        // Auto White Balance enable (0 or 1)
  //  //  s->set_exposure_ctrl(s, 0);                   // auto exposure off
  //  //  s->set_brightness(s, 0);  // (-2 to 2) - set brightness
  //  //  s->set_agc_gain(s, 0);          // set gain manually (0 - 30)
  //  //  s->set_aec_value(s, 0);     // set exposure manually  (0-1200)

  //  //s->set_vflip(s, 1);                               // flip image vertically
  //  //s->set_hmirror(s, 1);                             // flip image horizontally

  return 1;
}


//    // More camera settings available:
//    // If you enable gain_ctrl or exposure_ctrl it will prevent a lot of the other settings having any effect
//    // more info on settings here: https://randomnerdtutorials.com/esp32-cam-ov2640-camera-settings/
//    s->set_gain_ctrl(s, 0);                       // auto gain off (1 or 0)
//    s->set_exposure_ctrl(s, 0);                   // auto exposure off (1 or 0)
//    s->set_agc_gain(s, 1);                        // set gain manually (0 - 30)
//    s->set_aec_value(s, 1);                       // set exposure manually  (0-1200)
//    s->set_vflip(s, 1);                           // Invert image (0 or 1)
//    s->set_quality(s, 10);                        // (0 - 63)
//    s->set_gainceiling(s, GAINCEILING_32X);       // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
//    s->set_brightness(s, 0);                      // (-2 to 2) - set brightness
//    s->set_lenc(s, 1);                            // lens correction? (1 or 0)
//    s->set_saturation(s, 0);                      // (-2 to 2)
//    s->set_contrast(s, 0);                        // (-2 to 2)
//    s->set_sharpness(s, 0);                       // (-2 to 2)
//    s->set_hmirror(s, 0);                         // (0 or 1) flip horizontally
//    s->set_colorbar(s, 0);                        // (0 or 1) - show a testcard
//    s->set_special_effect(s, 0);                  // (0 to 6?) apply special effect
//    s->set_whitebal(s, 0);                        // white balance enable (0 or 1)
//    s->set_awb_gain(s, 1);                        // Auto White Balance enable (0 or 1)
//    s->set_wb_mode(s, 0);                         // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
//    s->set_dcw(s, 0);                             // downsize enable? (1 or 0)?
//    s->set_raw_gma(s, 1);                         // (1 or 0)
//    s->set_aec2(s, 0);                            // automatic exposure sensor?  (0 or 1)
//    s->set_ae_level(s, 0);                        // auto exposure levels (-2 to 2)
//    s->set_bpc(s, 0);                             // black pixel correction
//    s->set_wpc(s, 0);                             // white pixel correction
