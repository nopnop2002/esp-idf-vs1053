# esp-idf-vs1053
VS1053 Driver for esp-idf.    
You can listen Internet radio.   
I ported from [here](https://github.com/baldram/ESP_VS1053_Library).

# Software requirements
ESP-IDF V5.0 or later.   
ESP-IDF V4.4 release branch reached EOL in July 2024.   

# Hardware requirements
VS1003 or VS1053 Development Board.   
The performance of VS1003 (Blue Board) and VS1053 is completely different.   
__VS1003 (Blue Board) is not fast.__   

# Instalation
```
git clone https://github.com/nopnop2002/esp-idf-vs1053
cd esp-idf-vs1053
idf.py set-target esp32
idf.py menuconfig
idf.py flash monitor
```

![config-main](https://user-images.githubusercontent.com/6020549/127245065-079abcc4-a94e-4590-a0b3-c796736e3b17.jpg)
![Image](https://github.com/user-attachments/assets/979bf5ad-7fdc-4e20-98a5-8038d803b9d5)

# Configuration   
You have to set this config value with menuconfig.   

## WiFi Setting   
- CONFIG_ESP_WIFI_SSID   
SSID of your wifi.
- CONFIG_ESP_WIFI_PASSWORD   
PASSWORD of your wifi.
- CONFIG_ESP_MAXIMUM_RETRY   
Maximum number of retries when connecting to wifi.

![config-wifi](https://user-images.githubusercontent.com/6020549/127245171-de3036c4-505b-4915-9f3f-774fe3bfa5b0.jpg)

## VS1053 Setting   
- CONFIG_GPIO_CS   
GPIO for XCS of VS1003.
- CONFIG_GPIO_DCS   
GPIO for XDCS of VS1003.
- CONFIG_GPIO_DREQ   
GPIO for XDREQ of VS1003.
- CONFIG_GPIO_RESET   
GPIO for XRST of VS1003.Normally use the EN pin.
- CONFIG_VOLUME   
Volume of VS1003.

![config-vs1053](https://user-images.githubusercontent.com/6020549/127245221-01499f85-cb86-49e0-af16-9468ff25b5d4.jpg)

## Radio Station Setting   
- CONFIG_SERVER_HOST   
Play [this internet radio](https://somafm.com/player/#/now-playing/seventies).   
- CONFIG_SERVER_PORT   
- CONFIG_SERVER_PATH   
- CONFIG_METADATA_OUTPUT   
See Display Metadata section.   

![config-radio-1](https://user-images.githubusercontent.com/6020549/127245287-34956f6e-cdbe-497e-954e-fdbb31ffb5a3.jpg)


# Wireing  

# for Blue board(VS1003)   

|VS1003||ESP32|
|:-:|:-:|:-:|
|5V|--|VIN(*1)|
|DGND|--|GND|
|MISO|--|GPIO19|
|MOSI|--|GPIO23|
|SCK|--|GPIO18|
|DREQ|--|GPIO4(*2)|
|XRST|--|EN(*2)|
|XCS|--|GPIO5(*2)|
|XDCS|--|GPIO16(*2)|

(*1) External power if no VIN Pin.   
(*2) You can change any GPIO using menuconfig.   

![vs1053-1](https://user-images.githubusercontent.com/6020549/76676544-0782b700-6608-11ea-8b79-6e77598f7b8e.JPG)

![vs1053-2](https://user-images.githubusercontent.com/6020549/76676546-09e51100-6608-11ea-8a78-104490b97406.JPG)


# for Red board(VS1053)   

|VS1053||ESP32|
|:-:|:-:|:-:|
|5V|--|VIN(*1)|
|GND|--|GND|
|CS|--|N/C(*3)|
|MISO|--|GPIO19|
|SI|--|GPIO23|
|SCK|--|GPIO18|
|XCS|--|GPIO5(*2)|
|XRESET|--|EN(*2)|
|XDCS|--|GPIO16(*2)|
|DREQ|--|GPIO4(*2)|

(*1) External power if no VIN Pin.   
(*2) You can change any GPIO using menuconfig.   
(*3) For SD card reader on the back   

![vs1053-1](https://user-images.githubusercontent.com/6020549/78961282-e42a2980-7b2b-11ea-97d1-7fbf7317189a.JPG)


# for Green board(VS1053)   
Same as Blue board.   

![vs1053-GREEN](https://user-images.githubusercontent.com/6020549/81255518-cd162300-9068-11ea-9021-7fb3b2d09491.JPG)

# Chip identification   

- VS1003
```
I (2135) VS1053: REG     Contents
I (2135) VS1053: ---     -----
I (2145) VS1053:   0 -   800
I (2155) VS1053:   1 -    38 ---- > 0x3X is VS1003
I (2165) VS1053:   2 -     0
I (2175) VS1053:   3 -     0
I (2185) VS1053:   4 -     0
I (2195) VS1053:   5 -  1F40
I (2205) VS1053:   6 -     0
I (2215) VS1053:   7 -     0
I (2225) VS1053:   8 -     0
I (2235) VS1053:   9 -     0
I (2245) VS1053:   A -     0
I (2255) VS1053:   B -     0
I (2265) VS1053:   C -     0
I (2275) VS1053:   D -     0
I (2285) VS1053:   E -     0
I (2295) VS1053:   F -     0

```

- VS1053
```
I (17237) VS1053: REG    Contents
I (17237) VS1053: ---    -----
I (17247) VS1053:   0 -  4800
I (17257) VS1053:   1 -    40 ---- > 0x4X is VS1053
I (17267) VS1053:   2 -     0
I (17277) VS1053:   3 -  6000
I (17287) VS1053:   4 -     0
I (17297) VS1053:   5 -  AC44
I (17307) VS1053:   6 -     0
I (17317) VS1053:   7 -  1E06
I (17327) VS1053:   8 -     0
I (17337) VS1053:   9 -     0
I (17347) VS1053:   A -     0
I (17357) VS1053:   B -  FFFC
I (17367) VS1053:   C -     0
I (17377) VS1053:   D -     0
I (17387) VS1053:   E -     0
I (17397) VS1053:   F -     0
```

With the VS1003, radio stations larger than 128K bit rate cannot be played for a long time.   
VS1003 and VS1053 have completely different performance.   
Click [here](https://github.com/nopnop2002/esp-idf-vs1053/issues/2) for details.

# About embedded metadata
SHOUTCast server can put a Metadata Chunk in the middle of StreamData.   
The Metadata Chunk contains song titles and radio station information.

## Request embedded metadata chunk
Include the following in the HTTP Request.
```
Icy-MetaData: 1
```

## Metadata chunk interval
The SHOUTcast server will notify the metadata interval below.
```
icy-metaint:45000
```
This means that embedded metadata is sent from the server every 45000 bytes.
```
 --------------------------------------------------------------------------------
 |<---45000Byte Stream data---><Metadata><---45000Byte Stream data---><Metadata>
 --------------------------------------------------------------------------------
```

## Metadata chunk format
[Here](https://stackoverflow.com/questions/44050266/get-info-from-streaming-radio) is a detailed explanation.

The very first byte of the metadata chunk tells us how long the metadata chunk is.   
However, most are 0.   
0 indicates that the metadata chunk is 0 blocks(=0 byte)
```
 --------------------------------------------------------------------------------
 |<---45000Byte Stream data---><0><---45000Byte Stream data---><0>
 --------------------------------------------------------------------------------
```

## Display Metadata
The detected Metadata is sent to the CONSOLE task via RingBuffer.   
CONSOLE task display example:   
```
I (3479002) CONSOLE: xRingbufferReceive item_size=112
I (3479012) CONSOLE:
StreamTitle='Maria Muldaur - Sweet Harmony';StreamUrl='http://somafm.com/logos/512/seventies512.jpg';
```

By changing the CONSOLE task, the received Metadata can be displayed on an external monitor.   
These pages will be helpful.

- HD44780(GPIO)
https://github.com/UncleRus/esp-idf-lib/tree/master/examples/hd44780_gpio

- HD44780(I2C)
https://github.com/UncleRus/esp-idf-lib/tree/master/examples/hd44780_i2c

- SSD1306
https://github.com/nopnop2002/esp-idf-ssd1306

__Note:__   
If you use the SPI interface for this purpose, you need to use a __VSPI_HOST(SPI3_HOST) device__.   
Because VS1053 occupies HSPI_HOST(SPI2_HOST) device.   

__My recommendation:__   
My recommendation is to transfer the detected metadata to another ESP on the network and view it on another ESP.   
The simplest implementation is UDP Broadcast.   
You can select Metadata output destination using menuconfig.   
![config-radio-2](https://user-images.githubusercontent.com/6020549/137661606-14c7c6d3-a07f-4824-8dee-135bc30f684c.jpg)

If you select UDP Broadcast, you can specify the UDP port number.   
![config-radio-3](https://user-images.githubusercontent.com/6020549/137661607-41721c12-6ed7-4a66-a790-a68c166f9326.jpg)

You can use udp_receive.py to receive Broadcast metadata.   
UDP broadcast data can be received by ESP32/ESP8266.   
![udp-receive](https://user-images.githubusercontent.com/6020549/120304206-8ac22280-c30a-11eb-9d81-f9a09ba43db1.jpg)

# About Transfer-Encoding: chunked
There is some radio station return [Transfer-Encoding: chunked].   
This is one of them.

```
host = "icecast.radiofrance.fr";
path = "/franceculture-lofi.mp3";
Port = 80;
```

Even if you request [Icy-MetaData: 1], there is no [Icy-metaint] in the responce.   
Chunks are padded periodically.   
Details of [Transfer-Encoding: chunked] is [here](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding).  
```
 -------------------------------------------------------------------------------------------------
 |<StreamSize><CR><LF><---Stream data---><CR><LF><StreamSize><CR><LF><---Stream data---><CR><LF>
 -------------------------------------------------------------------------------------------------

<StreamSize> is Hexadecimal string.

<1234> is 4660 bytes.
```

