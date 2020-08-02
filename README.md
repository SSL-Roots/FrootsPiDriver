# FrootsPiDriver
FrootsPiのデバイスドライバ

## Installation

### 簡単

```bash
$ sudo apt install linux-headers-$(uname -r) build-essential
$ git clone https://github.com/SSL-Roots/FrootsPiDriver
$ cd FrootsPiDriver/utils
$ ./start.bash
```

### ちょっと複雑

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

## Development

フォーマット

```bash
$ sudo apt install clang-format
$ cd FrootsPiDriver/srd/driver
$ clang-format -i frootspi-main.c
```