#!/usr/bin/python3

import time

PUSHSW_PATH_LIST = [
    '/dev/frootspi_pushsw0',
    '/dev/frootspi_pushsw1',
    '/dev/frootspi_pushsw2',
    '/dev/frootspi_pushsw3'
]
DIPSW_PATH_LIST = [
    '/dev/frootspi_dipsw0',
    '/dev/frootspi_dipsw1',
]
LED_PATH = '/dev/frootspi_led0'

# 指定されたプッシュスイッチが押されたらTrue
def pushsw_has_pressed(path):
    retval = False
    with open(path, 'r') as f:
        # 負論理回路のため、押されると0を返す
        if f.readline() == "0\n":
            retval = True
    return retval

# プッシュスイッチのうち1つでも押されたらTrue
def any_pushsw_has_pressed():
    for path in PUSHSW_PATH_LIST:
        if pushsw_has_pressed(path):
            return True

    return False

# 指定されたDIPスイッチの状態を1/0で返す
def get_dipsw_state(path):
    retval = 0
    with open(path, 'r') as f:
        # 負論理回路のため、ONになると0を返す
        if f.readline() == "0\n":
            retval = 1
    return retval

# DIPスイッチの状態が変わったらTrue
g_dipsw_state = 0
def dipsw_state_has_changed():
    global g_dipsw_state

    current_dipsw_state = get_dipsw_state(DIPSW_PATH_LIST[0])
    current_dipsw_state += 2 * get_dipsw_state(DIPSW_PATH_LIST[1])

    if current_dipsw_state != g_dipsw_state:
        g_dipsw_state = current_dipsw_state
        return True

    return False

# LEDを点灯・消灯する
def turn_on_led(turn_on = False):
    with open(LED_PATH, 'w') as f:
        if turn_on:
            f.write('1')
            print("LED ON")
        else:
            f.write('0')
            print("LED OFF")

# LEDの点灯・消灯をトグルする
g_led_turn_on = False
def toggle_led():
    global g_led_turn_on
    g_led_turn_on = not g_led_turn_on
    turn_on_led(g_led_turn_on)

### main ###
if __name__ == '__main__':
    print("プッシュスイッチかDIPSWを押してね ([Ctrl-C]で終了)")
    dipsw_state_has_changed()  # DIPSW状態の初期化のため実行
    while 1:
        if any_pushsw_has_pressed():
            toggle_led()
            time.sleep(0.1)
        
        if dipsw_state_has_changed():
            toggle_led()

        time.sleep(0.1)
