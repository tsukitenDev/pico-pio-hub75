# PICO-PIO-HUB75

Raspberry Pi PicoでHUB75対応LEDマトリクスを制御するプログラム  
点灯周期制御・GPIO操作をPIOで行う



## About HUB75(E)
### HUB75(E) ピン配置
```
    /-----\
R1  | o o | G1
B1  | o o | GND
R2  | o o | G2
B2  \ o o | E
A   / o o | B
C   | o o | D
CLK | o o | LAT
OE  | o o | GND
    \-----/
```

### ピン設定

```
R0 - GPIO0
G0 - GPIO1
B0 - GPIO2
R1 - GPIO3
G1 - GPIO4
B1 - GPIO5

CLK - GPIO11

A - GPIO6
B - GPIO7
C - GPIO8
D - GPIO9
E - GPIO10

LAT - GPIO14
OE  - GPIO15
```

### 制御
入力は24ビットカラー、LUTを当ててガンマ補正(と8bit→10bitへ変換)を行う。  
１色10bitの疑似PWM制御により階調を表現する。
(256色を正確に表現するためには本来13-14bit必要だが、32bitに収めるため）  
PIO0は、ピクセル毎に特定の連続する3ビット(RGBの0/1)を取り出し、2ピクセル分のデータを送信する。これを1行分(実質は2行分)繰り返す。  


PIOに送るデータを
```
00BG RBGR BGRB GRBG RBGR BGRB GRBG RBGR
```
としておくとPIOで行うビットシフトがピクセル毎に1回で済むため高速。  
LUTで対応する。


PIO1はラッチの制御・LEDの点灯時間の制御を行う。



富士山の画像：Mocchy, Wikimedia Commons（パブリックドメイン）
https://commons.wikimedia.org/wiki/File:MtFuji_FujiCity.jpg
