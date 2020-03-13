# esp-idf-vs1053
VS1053 Driver for esp-idf.    
I ported from from [here](https://github.com/baldram/ESP_VS1053_Library).

# Software requirements
esp-idf ver4.1 or later.   

---

# Configure   
You have to set this config value with menuconfig.   
- CONFIG_ESP_WIFI_SSID   
- CONFIG_ESP_WIFI_PASSWORD   
- CONFIG_ESP_MAXIMUM_RETRY   
- CONFIG_GPIO_CS   
- CONFIG_GPIO_DCS   
- CONFIG_GPIO_DREQ   
- CONFIG_GPIO_RESET   
- CONFIG_SERVER_HOST   
- CONFIG_SERVER_PORT   
- CONFIG_SERVER_PATH   
```
git clone https://github.com/nopnop2002/esp-idf-vs1053
cd esp-idf-vs1053
make menuconfig
make flash
```

![config-1](https://user-images.githubusercontent.com/6020549/76663983-3415df00-65c6-11ea-93db-9ec83e2601df.jpg)
![config-2](https://user-images.githubusercontent.com/6020549/76663991-35dfa280-65c6-11ea-8ef4-0c2701ce6b48.jpg)

---

# Wireing  

|VS1053||ESP32|
|:-:|:-:|:-:|
|5V|--|VIN(*1)|
|DGND|--|GND|
|MISI|--|GPIO19|
|MOSI|--|GPIO23|
|SCK|--|GPIO18|
|DREQ|--|GPIO4(*2)|
|RST|--|EN(*2)|
|CS|--|GPIO5(*2)|
|DCS|--|GPIO16(*2)|

(*1) External power if no VIN Pin.   
(*2) You can change any GPIO using menuconfig.   


