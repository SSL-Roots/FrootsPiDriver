# FrootsPiDriver
FrootsPiのデバイスドライバ

## Installation

### Raspberry Pi のセットアップ

**Raspberry PiをFrootsPi基板に取り付ける前に**、
`/boot/firmware/usercfg.txt`に次の行を追加してください。

```sh
$ sudo vim /boot/firmware/usercfg.txt`
```

```txt
   enable_uart=0
   dtoverlay=spi1-3cs
   dtoverlay=pi3-disable-bt
   dtoverlay=mygpio
```

`enable_uart=0`でUARTをオフしないと、FrootsPi基板接続じにシリアルコンソールが起動してしまい、ログインできません。

`dtoverlay=spi1-3cs`と`dtoverlay=pi3-disable-bt`は、SPI1を使用するために必要です。
SPI1が使用できる代わりに**Bluetoothが使えなくなります**。

`dtoverlay=mygpio`はGPIOのプルアップ/プルダウンを設定するために必要です。
後ほど`mygpio.dtbo`を生成し、`/boot/firmware/overlays/`にコピーするスクリプトを実行します。

### FrootsPiDriverをインストールする（簡単）

```bash
$ sudo apt install linux-headers-$(uname -r) build-essential
$ git clone https://github.com/SSL-Roots/FrootsPiDriver
$ cd FrootsPiDriver/utils
$ ./start.bash
```

### FrootsPiDriverをインストールする（ちょっと複雑）

```bash
$ sudo apt install linux-headers-$(uname -r) build-essential

# モジュールのビルド
$ git clone https://github.com/SSL-Roots/FrootsPiDriver
$ cd FrootsPiDriver/srd/driver
$ make

# モジュールのインストール
$ sudo insmod frootspi.ko
# デバイスファイルに権限を与える
$ sudo chmod +x /dev/frootspi*

# デバッグメッセージの確認
$ dmesg

# モジュールのアンインストール
$ sudo rmmod frootspi
```

### GPIOのプルアップとプルダウンを設定する

```bash
$ cd FrootsPiDriver/utils
$ ./install_dtbo.bash
$ sudo reboot
```

## DeviceFiles

サンプルプログラム集 ([./samples](./samples)) も見てね。

### Hello World (/dev/frootspi_hello0 ~ 2)

デバイスドライバ（キャラクタデバイス）の練習用で作成したデバイスファイルです。

```sh
# 使い方
$ echo "HELLO FROOTSPI" > /dev/frootspi_hello0 
$ cat /dev/frootspi_hello0 
HELLO FROOTSPI
```

### プッシュスイッチ (/dev/frootspi_pushsw0 ~ 3)

プッシュスイッチの状態を取得します。
負論理回路なので`押されたら0`です。

```sh
# 使い方
$ cat /dev/frootspi_pushsw0 
1
```

### ディップスイッチ (/dev/frootspi_dipsw0, 1)

プッシュスイッチの状態を取得します。
負論理回路なので`ON = 0`です。

```sh
# 使い方
$ cat /dev/frootspi_dipsw0
1
```

### LED (/dev/frootspi_led0)

LEDを点灯・消灯させます。
`1 = 点灯`、`0 = 消灯`です。

```sh
# 使い方
$ echo 1 > /dev/frootspi_led0
$ echo 0 > /dev/frootspi_led0
```

## Development

フォーマットを整える方法

```bash
$ sudo apt install clang-format
$ cd FrootsPiDriver/srd/driver
$ clang-format -i frootspi_main.c
```

## その他

- License: GPL-2.0
- 参考資料
  - https://github.com/rt-net/RaspberryPiMouse
