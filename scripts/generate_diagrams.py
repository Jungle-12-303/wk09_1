#!/usr/bin/env python3
"""Generate 6 SVG diagrams for PintOS project documentation."""

import os
from pathlib import Path

# ---------------------------------------------------------------------------
# Design System
# ---------------------------------------------------------------------------
DS = {
    "bg": "#FFFFFF",
    "text_primary": "#191F28",
    "text_secondary": "#4E5968",
    "border_subtle": "#E5E8EB",
    "shadow": "#F2F4F6",
    "font": "system-ui, -apple-system, sans-serif",
    "rx_card": 12,
    "rx_section": 20,
    "title_size": 32,
    "header_size": 22,
    "body_size": 18,
    "small_size": 15,
    "stroke_w": 2,
    "phases": {
        1: {"bg": "#EFF6FF", "border": "#3B82F6", "text": "#1D4ED8", "label": "Phase 1: Alarm Clock"},
        2: {"bg": "#F0FDF4", "border": "#22C55E", "text": "#15803D", "label": "Phase 2: Priority"},
        3: {"bg": "#FFF7ED", "border": "#F97316", "text": "#C2410C", "label": "Phase 3: Donation"},
        4: {"bg": "#FAF5FF", "border": "#A855F7", "text": "#7E22CE", "label": "Phase 4: MLFQS"},
    },
}

OUT_DIR = Path(__file__).resolve().parent.parent / "docs" / "pintos" / "img"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def svg_header(w, h, title_text):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}"
     font-family="{DS['font']}">
  <rect width="{w}" height="{h}" fill="{DS['bg']}"/>
  <text x="{w//2}" y="52" text-anchor="middle" font-size="{DS['title_size']}" font-weight="bold" fill="{DS['text_primary']}">{title_text}</text>
"""

def svg_footer():
    return "</svg>\n"

def phase_color(p, key):
    return DS["phases"][p][key]

def card_shadow(x, y, w, h, rx=12):
    return f'  <rect x="{x+3}" y="{y+3}" width="{w}" height="{h}" rx="{rx}" fill="{DS["shadow"]}" />\n'

def card(x, y, w, h, fill="#FFFFFF", border="#E5E8EB", rx=12, stroke_w=2):
    s = card_shadow(x, y, w, h, rx)
    s += f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{border}" stroke-width="{stroke_w}" />\n'
    return s

def arrow_marker(marker_id="arrowhead", color="#4E5968"):
    return f"""  <defs>
    <marker id="{marker_id}" markerWidth="16" markerHeight="11" refX="14" refY="5.5" orient="auto">
      <polygon points="0 0, 16 5.5, 0 11" fill="{color}" />
    </marker>
  </defs>
"""

def arrow_line(x1, y1, x2, y2, marker_id="arrowhead", color="#4E5968", sw=2):
    return f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{sw}" marker-end="url(#{marker_id})" />\n'

def arrow_path(d, marker_id="arrowhead", color="#4E5968", sw=2):
    return f'  <path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}" marker-end="url(#{marker_id})" />\n'

def text(x, y, content, size=18, fill=None, anchor="start", weight="normal"):
    fill = fill or DS["text_primary"]
    return f'  <text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{content}</text>\n'

# ---------------------------------------------------------------------------
# 1. Architecture
# ---------------------------------------------------------------------------
def gen_architecture():
    W, H = 1200, 900
    s = svg_header(W, H, "PintOS Threads -- 모듈 구조")
    s += arrow_marker("arr", DS["text_secondary"])

    # Three top cards
    cards_info = [
        ("timer.c", 1, ["timer_sleep()", "timer_interrupt()", "thread_awake()", "mlfqs_recalc()"],  60),
        ("thread.c", 2, ["thread_create()", "thread_unblock()", "thread_yield()", "thread_set_priority()"], 420),
        ("synch.c", 3, ["lock_acquire()", "lock_release()", "sema_down()", "sema_up()"], 780),
    ]
    cw, ch = 320, 280
    cy = 100
    for name, phase, funcs, cx in cards_info:
        p = phase
        s += card(cx, cy, cw, ch, fill=phase_color(p, "bg"), border=phase_color(p, "border"))
        s += text(cx + cw // 2, cy + 35, name, DS["header_size"], phase_color(p, "text"), "middle", "bold")
        s += f'  <line x1="{cx+20}" y1="{cy+50}" x2="{cx+cw-20}" y2="{cy+50}" stroke="{phase_color(p, "border")}" stroke-width="1" />\n'
        for i, fn in enumerate(funcs):
            s += text(cx + 30, cy + 85 + i * 32, fn, DS["body_size"], DS["text_primary"])

    # Arrows: timer.c -> thread.c
    s += arrow_line(380, 240, 420, 240, "arr", DS["text_secondary"], 2)
    s += text(390, 228, "call", DS["small_size"], DS["text_secondary"], "middle")
    # thread.c -> synch.c
    s += arrow_line(740, 240, 780, 240, "arr", DS["text_secondary"], 2)
    s += text(750, 228, "call", DS["small_size"], DS["text_secondary"], "middle")
    # synch.c -> thread.c (back)
    s += arrow_path(f"M 780,290 C 760,320 740,320 740,290", "arr", DS["text_secondary"], 2)

    # Phase 4 full-width card
    p4y = 430
    s += card(60, p4y, 1040, 200, fill=phase_color(4, "bg"), border=phase_color(4, "border"), rx=DS["rx_section"])
    s += text(580, p4y + 40, "Phase 4: MLFQS -- 전체 모듈에 걸친 변경", DS["header_size"], phase_color(4, "text"), "middle", "bold")
    cols = [
        ("timer.c", "mlfqs_increment()\nmlfqs_recalc()\nmlfqs_priority()"),
        ("thread.c", "thread_set_nice()\nthread_get_nice()\nthread_get_recent_cpu()"),
        ("synch.c", "priority donation 비활성화\n(MLFQS 모드)"),
    ]
    for i, (label, desc) in enumerate(cols):
        cx = 110 + i * 340
        s += text(cx, p4y + 85, label, DS["body_size"], phase_color(4, "text"), "start", "bold")
        for j, line in enumerate(desc.split("\n")):
            s += text(cx, p4y + 115 + j * 28, line, DS["small_size"], DS["text_secondary"])

    # Legend
    ly = 680
    s += text(60, ly, "범례", DS["body_size"], DS["text_primary"], "start", "bold")
    for i, (pn, pinfo) in enumerate(DS["phases"].items()):
        lx = 60 + i * 280
        s += f'  <rect x="{lx}" y="{ly+12}" width="20" height="20" rx="4" fill="{pinfo["bg"]}" stroke="{pinfo["border"]}" stroke-width="2" />\n'
        s += text(lx + 30, ly + 28, pinfo["label"], DS["small_size"], pinfo["text"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 2. Phase Dependency
# ---------------------------------------------------------------------------
def gen_phase_dependency():
    W, H = 1200, 1000
    s = svg_header(W, H, "Phase 의존 관계")
    s += arrow_marker("arr_dep", DS["text_secondary"])

    phases_data = [
        (1, "Phase 1: Alarm Clock", "timer_sleep 재구현, sleep_list 도입", "busy-wait 제거"),
        (2, "Phase 2: Priority Scheduling", "ready_list 우선순위 정렬, 선점 체크", "높은 우선순위 먼저 실행"),
        (3, "Phase 3: Priority Donation", "lock에서 우선순위 기부/회수", "우선순위 역전 방지"),
        (4, "Phase 4: MLFQS", "nice, recent_cpu, load_avg로 자동 계산", "공정한 CPU 분배"),
    ]

    cw, ch = 800, 140
    cx = (W - cw) // 2
    start_y = 100

    for idx, (pn, title, desc, output) in enumerate(phases_data):
        cy = start_y + idx * 200
        p = DS["phases"][pn]
        s += card(cx, cy, cw, ch, fill=p["bg"], border=p["border"], rx=DS["rx_section"])
        s += text(cx + 40, cy + 40, title, DS["header_size"], p["text"], "start", "bold")
        s += text(cx + 40, cy + 75, desc, DS["body_size"], DS["text_primary"])
        # output badge
        s += f'  <rect x="{cx+40}" y="{cy+92}" width="{len(output)*13+20}" height="30" rx="15" fill="{p["border"]}20" stroke="{p["border"]}" stroke-width="1" />\n'
        s += text(cx + 50, cy + 113, output, DS["small_size"], p["text"])

        if idx < 3:
            ay = cy + ch
            s += arrow_line(W // 2, ay, W // 2, ay + 60, "arr_dep", p["border"], 3)

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 3. Thread Lifecycle
# ---------------------------------------------------------------------------
def gen_thread_lifecycle():
    W, H = 1200, 850
    s = svg_header(W, H, "스레드 생명 주기")
    s += arrow_marker("arr_lc", DS["text_secondary"])

    states = {
        "BLOCKED": (150, 400, DS["phases"][1]),
        "READY":   (500, 200, DS["phases"][2]),
        "RUNNING": (850, 400, DS["phases"][2]),
        "DYING":   (500, 650, DS["phases"][4]),
    }
    sw, sh = 220, 100

    for name, (cx, cy, p) in states.items():
        x, y = cx - sw // 2, cy - sh // 2
        s += card(x, y, sw, sh, fill=p["bg"], border=p["border"], rx=DS["rx_section"])
        s += text(cx, cy + 8, name, DS["header_size"], p["text"], "middle", "bold")

    # Arrows
    transitions = [
        ("BLOCKED", "READY",   "thread_unblock()\nsema_up()", 1),
        ("READY",   "RUNNING", "schedule()", 2),
        ("RUNNING", "BLOCKED", "sema_down()\nlock_acquire()", 3),
        ("RUNNING", "READY",   "thread_yield()", 2),
        ("RUNNING", "DYING",   "thread_exit()", 4),
    ]
    for src, dst, label, phase in transitions:
        sx, sy = states[src][:2]
        dx, dy = states[dst][:2]
        # offset endpoints to card edges
        if src == "BLOCKED" and dst == "READY":
            s += arrow_line(sx + 110, sy - 50, dx - 110, dy + 50, "arr_lc", DS["text_secondary"], 2)
            s += text((sx + dx) // 2 - 40, (sy + dy) // 2 - 20, label.split("\n")[0], DS["small_size"], DS["text_secondary"])
            if len(label.split("\n")) > 1:
                s += text((sx + dx) // 2 - 40, (sy + dy) // 2 + 2, label.split("\n")[1], DS["small_size"], DS["text_secondary"])
            # Phase badge
            p = DS["phases"][phase]
            s += f'  <rect x="{(sx+dx)//2 - 60}" y="{(sy+dy)//2 + 8}" width="80" height="22" rx="11" fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="1" />\n'
            s += text((sx + dx) // 2 - 20, (sy + dy) // 2 + 24, f"Phase {phase}", 12, p["text"], "middle")
        elif src == "READY" and dst == "RUNNING":
            s += arrow_line(dx - 110, dy - 50, dx - 110, dy - 50, "arr_lc")
            s += arrow_line(sx + 110, sy + 50, dx - 110, dy - 50, "arr_lc", DS["text_secondary"], 2)
            lx, ly_ = (sx + dx) // 2 + 30, (sy + dy) // 2 + 10
            s += text(lx, ly_, label, DS["small_size"], DS["text_secondary"])
            p = DS["phases"][phase]
            s += f'  <rect x="{lx - 10}" y="{ly_ + 6}" width="80" height="22" rx="11" fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="1" />\n'
            s += text(lx + 30, ly_ + 22, f"Phase {phase}", 12, p["text"], "middle")
        elif src == "RUNNING" and dst == "BLOCKED":
            s += arrow_line(sx - 110, sy - 50, dx + 110, dy - 50, "arr_lc", DS["text_secondary"], 2)
            lx, ly_ = (sx + dx) // 2 - 20, (sy + dy) // 2 - 60
            s += text(lx, ly_, label.split("\n")[0], DS["small_size"], DS["text_secondary"])
            if len(label.split("\n")) > 1:
                s += text(lx, ly_ + 20, label.split("\n")[1], DS["small_size"], DS["text_secondary"])
            p = DS["phases"][phase]
            s += f'  <rect x="{lx - 10}" y="{ly_ + 26}" width="80" height="22" rx="11" fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="1" />\n'
            s += text(lx + 30, ly_ + 42, f"Phase {phase}", 12, p["text"], "middle")
        elif src == "RUNNING" and dst == "READY":
            s += arrow_path(f"M {sx-60},{sy-50} C {sx-60},{sy-200} {dx+60},{dy+200} {dx+60},{dy+50}", "arr_lc", DS["text_secondary"], 2)
            s += text(sx - 20, sy - 160, label, DS["small_size"], DS["text_secondary"])
            p = DS["phases"][phase]
            s += f'  <rect x="{sx - 30}" y="{sy - 152}" width="80" height="22" rx="11" fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="1" />\n'
            s += text(sx + 10, sy - 136, f"Phase {phase}", 12, p["text"], "middle")
        elif src == "RUNNING" and dst == "DYING":
            s += arrow_line(sx, sy + 50, dx, dy - 50, "arr_lc", DS["text_secondary"], 2)
            lx, ly_ = (sx + dx) // 2 + 20, (sy + dy) // 2
            s += text(lx, ly_, label, DS["small_size"], DS["text_secondary"])

    # create -> READY
    s += arrow_line(350, 120, 460, 160, "arr_lc", DS["text_secondary"], 2)
    s += text(320, 110, "thread_create()", DS["small_size"], DS["text_secondary"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 4. Timer Interrupt
# ---------------------------------------------------------------------------
def gen_timer_interrupt():
    W, H = 1200, 950
    s = svg_header(W, H, "timer_interrupt() 처리 흐름")
    s += arrow_marker("arr_ti", DS["text_secondary"])

    # Root
    root_x, root_y = 400, 90
    rw, rh = 400, 50
    s += card(root_x, root_y, rw, rh, fill="#F8FAFC", border=DS["border_subtle"])
    s += text(root_x + rw // 2, root_y + 33, "timer_interrupt()", DS["header_size"], DS["text_primary"], "middle", "bold")

    # Level 1 items
    items_l1 = [
        (None, "ticks++", None, 190),
        (None, "thread_tick() -- 선점 판단", None, 280),
        (1, "thread_awake() -- sleep 스레드 깨우기", "sleep_list 순회, wake_tick <= ticks 인 스레드 unblock", 370),
    ]

    lx_base = 160
    cw_l1 = 880

    for phase, title, desc, yy in items_l1:
        if phase:
            p = DS["phases"][phase]
            s += card(lx_base, yy, cw_l1, 90 if desc else 50, fill=p["bg"], border=p["border"])
            # Phase badge
            s += f'  <rect x="{lx_base + 15}" y="{yy + 12}" width="80" height="26" rx="13" fill="{p["border"]}" />\n'
            s += text(lx_base + 55, yy + 31, f"Phase {phase}", 13, "#FFFFFF", "middle", "bold")
            s += text(lx_base + 110, yy + 33, title, DS["body_size"], p["text"], "start", "bold")
            if desc:
                s += text(lx_base + 110, yy + 65, desc, DS["small_size"], DS["text_secondary"])
        else:
            s += card(lx_base, yy, cw_l1, 50, fill="#F8FAFC", border=DS["border_subtle"])
            s += text(lx_base + 30, yy + 33, title, DS["body_size"], DS["text_primary"])
        # connector
        s += arrow_line(600, yy - 20 if yy > 190 else 140, 600, yy, "arr_ti", DS["text_secondary"], 2)

    # Phase 4 MLFQS block
    mlfqs_y = 510
    p4 = DS["phases"][4]
    s += card(lx_base, mlfqs_y, cw_l1, 380, fill=p4["bg"], border=p4["border"], rx=DS["rx_section"])
    s += f'  <rect x="{lx_base + 15}" y="{mlfqs_y + 15}" width="80" height="26" rx="13" fill="{p4["border"]}" />\n'
    s += text(lx_base + 55, mlfqs_y + 34, "Phase 4", 13, "#FFFFFF", "middle", "bold")
    s += text(lx_base + 110, mlfqs_y + 36, "MLFQS 연산", DS["header_size"], p4["text"], "start", "bold")
    s += arrow_line(600, 460, 600, mlfqs_y, "arr_ti", DS["text_secondary"], 2)

    sub_items = [
        ("매 틱", "recent_cpu += 1 (현재 스레드)", mlfqs_y + 80),
        ("매 TIMER_FREQ (1초)", "load_avg 재계산\nrecent_cpu 전체 스레드 재계산", mlfqs_y + 150),
        ("매 4틱", "priority 재계산 (전체 스레드)", mlfqs_y + 280),
    ]

    for label, desc, yy in sub_items:
        s += card(lx_base + 40, yy, 800, 70 if "\n" not in desc else 100, fill="#FFFFFF", border=p4["border"], rx=10)
        # timing badge
        bw = len(label) * 13 + 20
        s += f'  <rect x="{lx_base + 60}" y="{yy + 12}" width="{bw}" height="26" rx="13" fill="{p4["bg"]}" stroke="{p4["border"]}" stroke-width="1" />\n'
        s += text(lx_base + 60 + bw // 2, yy + 31, label, 14, p4["text"], "middle", "bold")
        for j, line in enumerate(desc.split("\n")):
            s += text(lx_base + 80 + bw + 10, yy + 31 + j * 24, line, DS["body_size"], DS["text_primary"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 5. Priority Donation
# ---------------------------------------------------------------------------
def gen_priority_donation():
    W, H = 1200, 900
    s = svg_header(W, H, "Priority Donation 타임라인")
    s += arrow_marker("arr_pd", DS["text_secondary"])

    p3 = DS["phases"][3]

    # Timeline axis
    tl_y = 300
    s += f'  <line x1="80" y1="{tl_y}" x2="1120" y2="{tl_y}" stroke="{DS["border_subtle"]}" stroke-width="3" />\n'

    events = [
        ("T1", 160, "L(pri=1)이\nLock A 획득", "1"),
        ("T2", 420, "H(pri=63)이\nLock A 요청", "63->L 기부"),
        ("T3", 680, "L(pri=63) 실행 완료\nLock A 해제", "1 복원"),
        ("T4", 940, "H(pri=63)\nLock A 획득", "63"),
    ]

    for label, x, desc, pri_change in events:
        # Timeline dot
        s += f'  <circle cx="{x}" cy="{tl_y}" r="10" fill="{p3["border"]}" />\n'
        s += text(x, tl_y - 20, label, DS["header_size"], p3["text"], "middle", "bold")
        # Description card above
        lines = desc.split("\n")
        cw, ch = 200, 40 + len(lines) * 24
        cx = x - cw // 2
        cy = tl_y - 70 - ch
        s += card(cx, cy, cw, ch, fill=p3["bg"], border=p3["border"], rx=10)
        for i, line in enumerate(lines):
            s += text(x, cy + 28 + i * 24, line, DS["small_size"], DS["text_primary"], "middle")
        # Priority badge below
        s += f'  <rect x="{x-40}" y="{tl_y+20}" width="80" height="28" rx="14" fill="{p3["border"]}" />\n'
        s += text(x, tl_y + 40, f"pri={pri_change}", 14, "#FFFFFF", "middle", "bold")

    # Comparison section
    comp_y = 430
    s += text(600, comp_y, "비교", DS["header_size"], DS["text_primary"], "middle", "bold")

    # Without donation
    s += card(80, comp_y + 20, 1040, 140, fill="#FEF2F2", border="#EF4444", rx=DS["rx_card"])
    s += text(120, comp_y + 55, "기부 없을 때 (역전 발생)", DS["body_size"], "#DC2626", "start", "bold")
    # Timeline bars
    bar_y = comp_y + 75
    s += f'  <rect x="120" y="{bar_y}" width="300" height="30" rx="6" fill="#FECACA" stroke="#EF4444" stroke-width="1" />\n'
    s += text(270, bar_y + 21, "L(pri=1) 실행 중 -- Lock A 보유", 13, "#DC2626", "middle")
    s += f'  <rect x="450" y="{bar_y}" width="80" height="30" rx="6" fill="#FED7AA" stroke="#F97316" stroke-width="1" />\n'
    s += text(490, bar_y + 21, "M(pri=31)", 12, "#C2410C", "middle")
    s += f'  <rect x="560" y="{bar_y}" width="300" height="30" rx="6" fill="#DBEAFE" stroke="#3B82F6" stroke-width="1" />\n'
    s += text(710, bar_y + 21, "H(pri=63) 대기 -- 역전!", 13, "#1D4ED8", "middle")
    s += text(900, bar_y + 50, "H가 L보다 늦게 실행됨", DS["small_size"], "#DC2626")

    # With donation
    s += card(80, comp_y + 180, 1040, 140, fill="#F0FDF4", border="#22C55E", rx=DS["rx_card"])
    s += text(120, comp_y + 215, "기부 있을 때 (해결)", DS["body_size"], "#15803D", "start", "bold")
    bar_y2 = comp_y + 235
    s += f'  <rect x="120" y="{bar_y2}" width="200" height="30" rx="6" fill="#BBF7D0" stroke="#22C55E" stroke-width="1" />\n'
    s += text(220, bar_y2 + 21, "L(pri=63) 기부받아 실행", 13, "#15803D", "middle")
    s += f'  <rect x="350" y="{bar_y2}" width="250" height="30" rx="6" fill="#DBEAFE" stroke="#3B82F6" stroke-width="1" />\n'
    s += text(475, bar_y2 + 21, "H(pri=63) 바로 실행", 13, "#1D4ED8", "middle")
    s += f'  <rect x="630" y="{bar_y2}" width="120" height="30" rx="6" fill="#FED7AA" stroke="#F97316" stroke-width="1" />\n'
    s += text(690, bar_y2 + 21, "M(pri=31)", 12, "#C2410C", "middle")
    s += text(790, bar_y2 + 50, "H가 빠르게 실행됨", DS["small_size"], "#15803D")

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 6. Struct Thread
# ---------------------------------------------------------------------------
def gen_struct_thread():
    W, H = 1200, 1050
    s = svg_header(W, H, "struct thread 필드 구성")

    cx, cw = 200, 800
    row_h = 48
    header_h = 55
    start_y = 90

    groups = [
        (None, "기본 필드 (Original)", [
            ("tid_t", "tid", "스레드 식별자"),
            ("enum thread_status", "status", "현재 상태"),
            ("char[16]", "name", "스레드 이름"),
            ("int", "priority", "우선순위 (0-63)"),
            ("struct list_elem", "elem", "리스트 요소"),
            ("struct intr_frame", "tf", "인터럽트 프레임"),
            ("unsigned", "magic", "스택 오버플로 감지"),
        ]),
        (1, "Phase 1 추가 필드", [
            ("int64_t", "wake_tick", "깨어날 틱"),
        ]),
        (3, "Phase 3 추가 필드", [
            ("int", "original_priority", "기부 전 원래 우선순위"),
            ("struct lock*", "wait_on_lock", "대기 중인 락"),
            ("struct list", "donations", "기부 목록"),
            ("struct list_elem", "donation_elem", "기부 리스트 요소"),
        ]),
        (4, "Phase 4 추가 필드", [
            ("int", "nice", "nice 값 (-20~20)"),
            ("int", "recent_cpu", "최근 CPU 사용량 (고정소수점)"),
        ]),
    ]

    y = start_y
    for phase, group_title, fields in groups:
        if phase:
            p = DS["phases"][phase]
            bg, border, txt = p["bg"], p["border"], p["text"]
        else:
            bg, border, txt = "#F8FAFC", DS["border_subtle"], DS["text_primary"]

        group_h = header_h + len(fields) * row_h + 15
        s += card(cx, y, cw, group_h, fill=bg, border=border, rx=DS["rx_card"])

        # Group header
        if phase:
            bw = 80
            s += f'  <rect x="{cx + 20}" y="{y + 14}" width="{bw}" height="26" rx="13" fill="{border}" />\n'
            s += text(cx + 20 + bw // 2, y + 33, f"Phase {phase}", 13, "#FFFFFF", "middle", "bold")
            s += text(cx + 20 + bw + 15, y + 35, group_title, DS["body_size"], txt, "start", "bold")
        else:
            s += text(cx + 20, y + 35, group_title, DS["body_size"], txt, "start", "bold")

        # Column headers
        col_type_x = cx + 30
        col_name_x = cx + 250
        col_desc_x = cx + 460
        hy = y + header_h
        s += text(col_type_x, hy, "Type", DS["small_size"], DS["text_secondary"], "start", "bold")
        s += text(col_name_x, hy, "Field", DS["small_size"], DS["text_secondary"], "start", "bold")
        s += text(col_desc_x, hy, "Description", DS["small_size"], DS["text_secondary"], "start", "bold")

        for i, (ftype, fname, fdesc) in enumerate(fields):
            fy = hy + 18 + i * row_h
            # subtle separator
            if i > 0:
                s += f'  <line x1="{cx+20}" y1="{fy - 12}" x2="{cx+cw-20}" y2="{fy - 12}" stroke="{border}" stroke-width="0.5" opacity="0.4" />\n'
            s += text(col_type_x, fy + 14, ftype, 15, DS["text_secondary"])
            s += text(col_name_x, fy + 14, fname, 16, DS["text_primary"], "start", "bold")
            s += text(col_desc_x, fy + 14, fdesc, 15, DS["text_secondary"])

        y += group_h + 16

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    diagrams = [
        ("01-architecture.svg", gen_architecture),
        ("02-phase-dependency.svg", gen_phase_dependency),
        ("03-thread-lifecycle.svg", gen_thread_lifecycle),
        ("04-timer-interrupt.svg", gen_timer_interrupt),
        ("05-priority-donation.svg", gen_priority_donation),
        ("06-struct-thread.svg", gen_struct_thread),
    ]

    for filename, gen_func in diagrams:
        path = OUT_DIR / filename
        svg_content = gen_func()
        path.write_text(svg_content, encoding="utf-8")
        print(f"[OK] {path}")

    print(f"\nAll {len(diagrams)} diagrams generated in {OUT_DIR}")


if __name__ == "__main__":
    main()
