# ESP8266 WITH LED STATUS
  - blink fast: wifi is disconnected
  - blink slow: wifi is connected but ntp is inactive
  - led off   : wifi and ntp is working

# GPIO Connections
```
8CH  |  ESP8266
VCC _____ 5VIN 
IN1 _____ D0
IN2 _____ D3
IN3 _____ D5
IN4 _____ D6
IN5 _____ D7
IN6 _____ D8 - attached 5 seconds after boot (otherwise leave empty)
IN7 _____ RX
IN8 _____ TX
GND _____ GND
```

# DS3231 RTC Module
```
RTC | ESP8266
SDA → D2
SCL → D1
VCC → 3.3V
GND → GND
```
