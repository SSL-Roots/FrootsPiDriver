# FrootsPiDriver
FrootsPiのデバイスドライバ

## Installation

```bash
$ sudo apt install linux-headers-$(uname -r) build-essential

# モジュールのビルド
$ git clone https://github.com/SSL-Roots/FrootsPiDriver
$ cd FrootsPiDriver/srd/driver
$ make

# モジュールのインストール
$ sudo insmod frootspi.ko

# デバッグメッセージの確認
$ dmesg

# モジュールのアンインストール
$ sudo rmmod frootspi
```

## Development

フォーマット

```bash
$ sudo apt install clang-format
$ cd FrootsPiDriver/srd/driver
$ clang-format -i frootspi-main.c
```