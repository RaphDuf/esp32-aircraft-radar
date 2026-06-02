# esp32-aircraft-radar
A personnal aircraft radar to put on your desk. 
This project is based on the [opensky-network](https://opensky-network.org/) API and is deploy on an ESP32C3 with oled screen from Aliexpress (you can find it [here](https://s.click.aliexpress.com/e/_c3SxFl2d)). 

## Project result
![img1](https://github.com/RaphDuf/esp32-aircraft-radar/edit/main/img/IMG8326.jpeg "IMG 1")

## Library
Like explianed in the ESP32 [documentation](https://spotpear.com/wiki/ESP32-C3-desktop-trinket-Mini-TV-Portable-Pendant-LVGL-1.44inch-LCD-ST7735.html), you need to add specific library. You can find theme [here](https://github.com/Spotpear/ESP32C3_1.44inch)

## Configuration 
All the configuration is done at the beginning of the script. 
- ssid and password must be complete with your wifi informations.
- You need to create an account on opensky to have 4000 API call per day and complete clientID and clientSecret in the code with your credentials.
- CENTER_LAT and CENTER_LON are the GPS coordinates of the location you wish to monitor. OFFSET is the diameter of the monitored area. 
