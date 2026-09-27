import random
import sys
import time

# ============ 可调参数（算法内核，全部按“步”定义） ============
C_TARGET         = 0.01      # c: 每步期望，检测 p > c
ETA              = 0.001     # CUSUM 容忍裕度（每步）
H_THRESH         = 5.0       # CUSUM 报警阈值
M_MAX            = 1000      # 两个 1 之间间隔上界（步）

NORMAL_PS        = [0.003, 0.005, 0.008]   # 非报警状态，每步 p
SWITCH_PROB      = 0.02      # 非报警状态间每事件后切换概率
ENTER_ALARM_PROB = 0.005     # 非报警 -> 报警 每事件后概率
ALARM_P          = 0.02      # 报警状态，每步 p
# =============================================================

# ============ 仅作用于界面呈现的参数 ============
DT               = 0.002     # 每步在终端上的节拍（秒）
LINE_WIDTH       = 80        # # 每行上限，避免刷屏
MAX_TIME_STEPS   = 200_000   # 安全上限（步）
# =============================================


def geometric_truncated(p, m):
    """每步概率 p 的几何分布，截断到上界 m 步。"""
    d = 1
    while d < m and random.random() > p:
        d += 1
    return d


def main():
    random.seed()

    state = random.randrange(len(NORMAL_PS))
    in_alarm = False

    C = 0.0
    t_steps = 0
    n_ones = 0
    chars_on_line = 0

    n_false_alarms = 0
    final_report = None

    enter_alarm_time = None
    enter_alarm_ones = None

    print(f"[参数] c={C_TARGET}/步, η={ETA}/步, h={H_THRESH}, M={M_MAX}")
    print(f"[参数] 非报警 p={NORMAL_PS}/步, 报警 p={ALARM_P}/步")
    print(f"[参数] DT={DT}s/步 (仅界面节拍), 每行最多 {LINE_WIDTH} 个 #")
    print("开始模拟...\n")

    try:
        while t_steps < MAX_TIME_STEPS:
            p = ALARM_P if in_alarm else NORMAL_PS[state]
            d = geometric_truncated(p, M_MAX)

            # --- d-1 个 0 ---
            broke = False
            for _ in range(d - 1):
                C = max(0.0, C - C_TARGET - ETA)
                t_steps += 1
                time.sleep(DT)          # ← 界面节拍

                if C >= H_THRESH:
                    if not in_alarm:
                        n_false_alarms += 1
                        if chars_on_line:
                            print()
                            chars_on_line = 0
                        print(f"[误报 #{n_false_alarms}] "
                              f"t={t_steps}步 ({t_steps*DT:.2f}s), "
                              f"1数={n_ones}, C={C:.3f}")
                        C = 0.0
                    elif final_report is None:
                        final_report = (t_steps, n_ones, C)
                        if chars_on_line:
                            print()
                            chars_on_line = 0
                        print(f"[准报] t={t_steps}步 ({t_steps*DT:.2f}s), "
                              f"1数={n_ones}, C={C:.3f}")
                        broke = True
                        break

                if t_steps >= MAX_TIME_STEPS:
                    broke = True
                    break

            if broke and final_report is not None:
                break
            if t_steps >= MAX_TIME_STEPS:
                break

            # --- 1 个 1 ---
            C = max(0.0, C + 1.0 - C_TARGET - ETA)
            t_steps += 1
            n_ones += 1
            sys.stdout.write('#')
            sys.stdout.flush()
            chars_on_line += 1
            if chars_on_line >= LINE_WIDTH:
                print()
                chars_on_line = 0
            time.sleep(DT)              # ← 界面节拍

            if C >= H_THRESH:
                if not in_alarm:
                    n_false_alarms += 1
                    if chars_on_line:
                        print()
                        chars_on_line = 0
                    print(f"[误报 #{n_false_alarms}] "
                          f"t={t_steps}步 ({t_steps*DT:.2f}s), "
                          f"1数={n_ones}, C={C:.3f}")
                    C = 0.0
                elif final_report is None:
                    final_report = (t_steps, n_ones, C)
                    if chars_on_line:
                        print()
                        chars_on_line = 0
                    print(f"[准报] t={t_steps}步 ({t_steps*DT:.2f}s), "
                          f"1数={n_ones}, C={C:.3f}")

            if final_report is not None:
                break

            # --- 事件后状态切换（静默） ---
            if not in_alarm:
                r = random.random()
                if r < ENTER_ALARM_PROB:
                    in_alarm = True
                    enter_alarm_time = t_steps
                    enter_alarm_ones = n_ones
                elif r < ENTER_ALARM_PROB + SWITCH_PROB:
                    others = [i for i in range(len(NORMAL_PS)) if i != state]
                    if others:
                        state = random.choice(others)

    except KeyboardInterrupt:
        print("\n[中断] 用户手动停止。")

    if chars_on_line:
        print()

    # ================= 总结 =================
    print("\n========== 总结 ==========")
    print(f"总步数: {t_steps}")
    print(f"总 1 数: {n_ones}")
    print(f"界面呈现总时长(按DT): {t_steps * DT:.2f} 秒")
    print(f"误报次数: {n_false_alarms}")

    if final_report is None:
        print("未产生准报。")
        return

    rt, rn, rc = final_report
    print(f"准报: t={rt}步 ({rt*DT:.4f}s), 1数={rn}, C={rc:.3f}")

    if enter_alarm_time is not None:
        dt_s = rt - enter_alarm_time
        dt_n = rn - enter_alarm_ones
        print(f"[真实] 进入报警状态: "
              f"t={enter_alarm_time}步 ({enter_alarm_time*DT:.4f}s), "
              f"1数={enter_alarm_ones}")
        print(f"[对比] 真实进入报警 -> 算法准报: "
              f"{dt_s} 步 = {dt_s*DT:.4f} 秒, {dt_n} 次事件(1)")


if __name__ == '__main__':
    main()