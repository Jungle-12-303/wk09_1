#!/usr/bin/env python3
"""PintOS 프로젝트 문서용 SVG 다이어그램 6종 생성.

Toss-style design: white cards, subtle shadows, minimal color.
"""

from pathlib import Path
from xml.sax.saxutils import escape

# ---------------------------------------------------------------------------
# Design Tokens (Toss Style)
# ---------------------------------------------------------------------------
BG = "#FFFFFF"
CARD_BG = "#FFFFFF"
CARD_BORDER = "#F2F4F6"
TEXT_PRIMARY = "#191F28"
TEXT_BODY = "#333D4B"
TEXT_CAPTION = "#8B95A1"
DIVIDER = "#F2F4F6"
ACCENT_BLUE = "#3B82F6"

BADGE = {
    "blue":   {"bg": "#EFF6FF", "border": "#3B82F6", "text": "#1D4ED8"},
    "green":  {"bg": "#F0FDF4", "border": "#22C55E", "text": "#15803D"},
    "orange": {"bg": "#FFF7ED", "border": "#F97316", "text": "#C2410C"},
    "purple": {"bg": "#FAF5FF", "border": "#A855F7", "text": "#7E22CE"},
    "red":    {"bg": "#FEF2F2", "border": "#EF4444", "text": "#DC2626"},
}

PHASE_COLOR = {1: "blue", 2: "green", 3: "orange", 4: "purple"}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"

OUT_DIR = Path(__file__).resolve().parent.parent / "docs" / "pintos" / "img"


# ---------------------------------------------------------------------------
# Shared Defs (shadow filter + arrow marker)
# ---------------------------------------------------------------------------
def defs_block():
    return (
        '  <defs>\n'
        '    <filter id="shadow" x="-4%" y="-4%" width="108%" height="108%">\n'
        '      <feDropShadow dx="0" dy="2" stdDeviation="6" flood-color="#000" flood-opacity="0.06"/>\n'
        '    </filter>\n'
        '    <marker id="arrow" markerWidth="12" markerHeight="8" refX="11" refY="4" orient="auto">\n'
        '      <polygon points="0 0, 12 4, 0 8" fill="#8B95A1"/>\n'
        '    </marker>\n'
        '  </defs>\n'
    )


def svg_open(w, h, title):
    return (
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}"\n'
        f'     font-family="{FONT}">\n'
        f'  <rect width="{w}" height="{h}" fill="{BG}"/>\n'
        + defs_block()
        + f'  <text x="{w // 2}" y="56" text-anchor="middle" font-size="28" '
        f'font-weight="bold" fill="{TEXT_PRIMARY}">{escape(title)}</text>\n'
    )


def svg_close():
    return '</svg>\n'


# ---------------------------------------------------------------------------
# Components
# ---------------------------------------------------------------------------
def card(x, y, w, h):
    return (
        f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" '
        f'fill="{CARD_BG}" stroke="{CARD_BORDER}" stroke-width="1" filter="url(#shadow)"/>\n'
    )


def badge_el(x, y, label, color_key, w=None):
    """Render a pill badge. Auto-width if w is None."""
    c = BADGE[color_key]
    if w is None:
        w = len(label) * 8 + 24
    return (
        f'  <rect x="{x}" y="{y}" width="{w}" height="24" rx="12" fill="{c["bg"]}"/>\n'
        f'  <text x="{x + w // 2}" y="{y + 16}" font-size="12" fill="{c["text"]}" '
        f'text-anchor="middle" font-weight="bold">{escape(label)}</text>\n'
    )


def phase_badge(x, y, pn):
    return badge_el(x, y, f"Phase {pn}", PHASE_COLOR[pn])


def text(x, y, content, size=16, fill=TEXT_BODY, anchor="start", weight="normal", ff=None):
    extra = f' font-family="monospace"' if ff == "mono" else ""
    return (
        f'  <text x="{x}" y="{y}" font-size="{size}" fill="{fill}" '
        f'text-anchor="{anchor}" font-weight="{weight}"{extra}>{escape(content)}</text>\n'
    )


def line_el(x1, y1, x2, y2, with_arrow=True):
    end = ' marker-end="url(#arrow)"' if with_arrow else ""
    return (
        f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="#8B95A1" stroke-width="1.5"{end}/>\n'
    )


def path_el(d, with_arrow=True):
    end = ' marker-end="url(#arrow)"' if with_arrow else ""
    return (
        f'  <path d="{d}" fill="none" stroke="#8B95A1" stroke-width="1.5"{end}/>\n'
    )


def divider(x1, y1, x2, y2):
    return (
        f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{DIVIDER}" stroke-width="1"/>\n'
    )


# ---------------------------------------------------------------------------
# 01 Architecture (1200x750)
# ---------------------------------------------------------------------------
def gen_01():
    W, H = 1200, 750
    s = svg_open(W, H, "Pintos Project 1 -- 모듈 구조")

    # Three cards side by side at y=100
    cw, ch = 320, 240
    gap = 40
    y0 = 100

    modules = [
        (80,  "timer.c",  1, ["timer_sleep()", "timer_interrupt()", "thread_awake()"]),
        (440, "thread.c", 2, ["thread_create()", "thread_unblock()", "thread_yield()", "schedule()"]),
        (800, "synch.c",  3, ["lock_acquire()", "lock_release()", "sema_down()", "sema_up()"]),
    ]

    for cx, name, pn, funcs in modules:
        s += card(cx, y0, cw, ch)
        # header
        s += text(cx + cw // 2, y0 + 40, name, 20, TEXT_PRIMARY, "middle", "bold")
        # badge
        s += phase_badge(cx + 30, y0 + 55, pn)
        # divider
        s += divider(cx + 20, y0 + 90, cx + cw - 20, y0 + 90)
        # function list
        for j, fn in enumerate(funcs):
            s += text(cx + 30, y0 + 120 + j * 28, fn, 14, TEXT_BODY, ff="mono")

    # Arrows between cards
    ay = y0 + ch // 2
    s += line_el(400, ay, 440, ay)
    s += text(420, ay - 10, "호출", 14, TEXT_CAPTION, "middle")
    s += line_el(760, ay, 800, ay)
    s += text(780, ay - 10, "호출", 14, TEXT_CAPTION, "middle")

    # Full-width Phase 4 card
    p4y = 400
    s += card(80, p4y, 1040, 160)
    s += phase_badge(110, p4y + 20, 4)
    s += text(110 + 80, p4y + 36, "MLFQS", 20, TEXT_PRIMARY, "start", "bold")
    s += text(110, p4y + 70, "timer.c, thread.c, synch.c 세 모듈에 걸쳐 변경 사항 발생", 16, TEXT_BODY)
    s += text(110, p4y + 100, "nice, recent_cpu, load_avg 기반 우선순위 자동 계산 / priority donation 비활성화",
              14, TEXT_CAPTION)
    s += text(110, p4y + 125, "매 틱: recent_cpu++ / 매 초: load_avg, recent_cpu 재계산 / 매 4틱: priority 재계산",
              14, TEXT_CAPTION)

    # Legend at y=620
    ly = 620
    legend_items = [
        (1, "Phase 1"),
        (2, "Phase 2"),
        (3, "Phase 3"),
        (4, "Phase 4"),
    ]
    lx = 80
    for pn, label in legend_items:
        s += phase_badge(lx, ly, pn)
        lx += len(f"Phase {pn}") * 8 + 24 + 20

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# 02 Phase Dependency (1000x900)
# ---------------------------------------------------------------------------
def gen_02():
    W, H = 1000, 900
    s = svg_open(W, H, "Phase 의존 관계")

    cw, ch = 700, 140
    cx = 150
    gap = 40

    phases = [
        (100, 1, "Alarm Clock", "timer_sleep 재구현, sleep_list 도입"),
        (280, 2, "Priority Scheduling", "ready_list 우선순위 정렬, 선점 체크"),
        (460, 3, "Priority Donation", "lock에서 우선순위 기부/회수"),
        (640, 4, "MLFQS", "nice, recent_cpu, load_avg로 자동 계산"),
    ]

    for cy, pn, title, desc in phases:
        s += card(cx, cy, cw, ch)
        s += phase_badge(cx + 30, cy + 24, pn)
        bw = len(f"Phase {pn}") * 8 + 24
        s += text(cx + 30 + bw + 16, cy + 40, title, 20, TEXT_PRIMARY, "start", "bold")
        s += text(cx + 30, cy + 80, desc, 16, TEXT_BODY)

    # Arrows between cards
    ax = 500
    for i, (cy, pn, _, _) in enumerate(phases[:-1]):
        s += line_el(ax, cy + ch, ax, phases[i + 1][0])

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# 03 Thread Lifecycle (1100x750)
# ---------------------------------------------------------------------------
def gen_03():
    W, H = 1100, 750
    s = svg_open(W, H, "스레드 생명 주기")

    sw, sh = 240, 80

    states = {
        "READY":   (400, 120),
        "BLOCKED": (80,  350),
        "RUNNING": (780, 350),
        "DYING":   (400, 580),
    }

    state_colors = {
        "READY":   BADGE["green"]["border"],
        "BLOCKED": BADGE["blue"]["border"],
        "RUNNING": BADGE["green"]["border"],
        "DYING":   TEXT_CAPTION,
    }

    for name, (x, y) in states.items():
        color = state_colors[name]
        # Card with colored left border accent
        s += (f'  <rect x="{x}" y="{y}" width="{sw}" height="{sh}" rx="16" '
              f'fill="{CARD_BG}" stroke="{color}" stroke-width="2" filter="url(#shadow)"/>\n')
        s += text(x + sw // 2, y + sh // 2 + 7, name, 20, TEXT_PRIMARY, "middle", "bold")

    # thread_create() -> READY
    rx, ry = states["READY"]
    s += text(rx + sw // 2, ry - 30, "thread_create()", 14, TEXT_CAPTION, "middle")
    s += line_el(rx + sw // 2, ry - 20, rx + sw // 2, ry)

    # BLOCKED -> READY
    bx, by = states["BLOCKED"]
    s += line_el(bx + sw, by, rx, ry + sh)
    mx, my = (bx + sw + rx) // 2, (by + ry + sh) // 2
    s += text(mx - 20, my - 12, "thread_unblock()", 14, TEXT_CAPTION, "middle")
    s += phase_badge(mx - 50, my - 5, 1)

    # READY -> RUNNING
    runx, runy = states["RUNNING"]
    s += line_el(rx + sw, ry + sh, runx, runy)
    mx2, my2 = (rx + sw + runx) // 2, (ry + sh + runy) // 2
    s += text(mx2 + 20, my2 - 12, "schedule()", 14, TEXT_CAPTION, "middle")
    s += phase_badge(mx2 - 20, my2 - 5, 2)

    # RUNNING -> BLOCKED
    s += path_el(
        f"M {runx},{runy + sh} C {runx - 80},{runy + sh + 100} {bx + sw + 80},{by + sh + 100} {bx + sw // 2},{by + sh}"
    )
    s += text(W // 2, runy + sh + 80, "sema_down()", 14, TEXT_CAPTION, "middle")
    s += phase_badge(W // 2 - 40, runy + sh + 85, 3)

    # RUNNING -> READY (curved arrow going up)
    s += path_el(
        f"M {runx + sw // 2},{runy} C {runx + sw // 2},{ry - 40} {rx + sw},{ry - 40} {rx + sw},{ry + 10}"
    )
    s += text(runx - 20, ry - 40, "thread_yield()", 14, TEXT_CAPTION, "middle")
    s += phase_badge(runx - 70, ry - 33, 2)

    # RUNNING -> DYING
    dx, dy = states["DYING"]
    s += line_el(runx + sw // 2, runy + sh, dx + sw, dy + sh // 2)
    s += text(runx + 20, dy + 20, "thread_exit()", 14, TEXT_CAPTION, "middle")

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# 04 Timer Interrupt (1100x850)
# ---------------------------------------------------------------------------
def gen_04():
    W, H = 1100, 850
    s = svg_open(W, H, "timer_interrupt() 처리 흐름")

    # Root card at top center
    rw, rh = 400, 60
    rx = (W - rw) // 2
    ry = 100
    s += card(rx, ry, rw, rh)
    s += text(rx + rw // 2, ry + 38, "timer_interrupt()", 20, TEXT_PRIMARY, "middle", "bold")

    # Child cards
    cx = 80
    cw = 940

    # 1: ticks++
    c1y = 200
    c1h = 50
    s += card(cx, c1y, cw, c1h)
    s += text(cx + 30, c1y + 32, "ticks++", 16, TEXT_BODY, "start", "normal", "mono")
    s += line_el(W // 2, ry + rh, W // 2, c1y)

    # 2: thread_tick()
    c2y = 280
    c2h = 50
    s += card(cx, c2y, cw, c2h)
    s += text(cx + 30, c2y + 32, "thread_tick() -- 타임 슬라이스 초과 시 선점", 16, TEXT_BODY)
    s += line_el(W // 2, c1y + c1h, W // 2, c2y)

    # 3: Phase 1 thread_awake
    c3y = 360
    c3h = 70
    s += card(cx, c3y, cw, c3h)
    s += phase_badge(cx + 20, c3y + 12, 1)
    bw = len("Phase 1") * 8 + 24
    s += text(cx + 20 + bw + 12, c3y + 28, "thread_awake() -- sleep 스레드 깨우기",
              16, TEXT_PRIMARY, "start", "bold")
    s += text(cx + 20 + bw + 12, c3y + 52,
              "sleep_list 순회, wake_tick <= ticks 인 스레드 unblock",
              14, TEXT_CAPTION)
    s += line_el(W // 2, c2y + c2h, W // 2, c3y)

    # 4: Phase 4 MLFQS large card
    c4y = 470
    c4h = 330
    s += card(cx, c4y, cw, c4h)
    s += phase_badge(cx + 20, c4y + 20, 4)
    bw4 = len("Phase 4") * 8 + 24
    s += text(cx + 20 + bw4 + 12, c4y + 36, "MLFQS 연산", 20, TEXT_PRIMARY, "start", "bold")
    s += line_el(W // 2, c3y + c3h, W // 2, c4y)

    # Sub-cards inside MLFQS
    sx = 110
    sw2 = 880

    # Sub 1: 매 틱
    s1y = 530
    s1h = 55
    s += card(sx, s1y, sw2, s1h)
    s += badge_el(sx + 20, s1y + 16, "매 틱", "purple")
    s += text(sx + 20 + 60 + 16, s1y + 34, "recent_cpu += 1 (현재 스레드)", 16, TEXT_BODY)

    # Sub 2: 매 초
    s2y = 610
    s2h = 75
    s += card(sx, s2y, sw2, s2h)
    s += badge_el(sx + 20, s2y + 16, "매 초", "purple")
    s += text(sx + 20 + 56 + 16, s2y + 34, "load_avg 재계산", 16, TEXT_BODY)
    s += text(sx + 20 + 56 + 16, s2y + 56, "recent_cpu 전체 스레드 재계산", 16, TEXT_BODY)

    # Sub 3: 매 4틱
    s3y = 710
    s3h = 55
    s += card(sx, s3y, sw2, s3h)
    s += badge_el(sx + 20, s3y + 16, "매 4틱", "purple")
    s += text(sx + 20 + 72 + 16, s3y + 34, "priority 재계산 (전체 스레드)", 16, TEXT_BODY)

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# 05 Priority Donation (1200x800)
# ---------------------------------------------------------------------------
def gen_05():
    W, H = 1200, 800
    s = svg_open(W, H, "Priority Donation 시나리오")

    # Horizontal timeline at y=300
    tl_y = 300
    s += (f'  <line x1="100" y1="{tl_y}" x2="1100" y2="{tl_y}" '
          f'stroke="{TEXT_CAPTION}" stroke-width="2"/>\n')

    events = [
        (200,  "L(pri=1)이\nLock A 획득",    "pri=1"),
        (450,  "H(pri=63)이\nLock A 요청",   "63 기부"),
        (700,  "L(pri=63) 완료\nLock A 해제", "pri=1 복원"),
        (950,  "H(pri=63)\nLock A 획득",     "pri=63"),
    ]

    ecw, ech = 180, 70
    for ex, lines_raw, badge_text in events:
        # Dot on timeline
        s += f'  <circle cx="{ex}" cy="{tl_y}" r="6" fill="{BADGE["orange"]["border"]}"/>\n'

        # Card above
        ecy = tl_y - 95
        ecx = ex - ecw // 2
        s += card(ecx, ecy, ecw, ech)
        parts = lines_raw.split("\n")
        s += text(ex, ecy + 30, parts[0], 14, TEXT_PRIMARY, "middle")
        if len(parts) > 1:
            s += text(ex, ecy + 50, parts[1], 14, TEXT_PRIMARY, "middle")

        # Connector
        s += (f'  <line x1="{ex}" y1="{ecy + ech}" x2="{ex}" y2="{tl_y - 6}" '
              f'stroke="{CARD_BORDER}" stroke-width="1"/>\n')

        # Badge below
        s += badge_el(ex - 50, tl_y + 20, badge_text, "orange", 100)

    # Comparison cards below
    comp_x = 80
    comp_w = 1040

    # Without donation (red left border)
    wy = 420
    wh = 120
    s += card(comp_x, wy, comp_w, wh)
    # Red left border accent
    s += (f'  <rect x="{comp_x}" y="{wy}" width="4" height="{wh}" rx="0" '
          f'fill="{BADGE["red"]["border"]}"/>\n')
    s += text(comp_x + 24, wy + 30, "기부 없을 때 (우선순위 역전 발생)", 16, TEXT_PRIMARY, "start", "bold")

    # Three bars
    bar_y = wy + 50
    bar_h = 28
    # L running long
    s += (f'  <rect x="{comp_x + 24}" y="{bar_y}" width="300" height="{bar_h}" rx="6" '
          f'fill="{BADGE["red"]["bg"]}" stroke="{BADGE["red"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 174, bar_y + 19, "L(pri=1) Lock A 보유", 12, BADGE["red"]["text"], "middle", "bold")
    # M interrupts
    s += (f'  <rect x="{comp_x + 340}" y="{bar_y}" width="200" height="{bar_h}" rx="6" '
          f'fill="{BADGE["orange"]["bg"]}" stroke="{BADGE["orange"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 440, bar_y + 19, "M(pri=31) 선점", 12, BADGE["orange"]["text"], "middle", "bold")
    # H waiting
    s += (f'  <rect x="{comp_x + 556}" y="{bar_y}" width="300" height="{bar_h}" rx="6" '
          f'fill="{BADGE["blue"]["bg"]}" stroke="{BADGE["blue"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 706, bar_y + 19, "H(pri=63) 대기 -- 역전!", 12, BADGE["blue"]["text"], "middle", "bold")
    s += text(comp_x + 880, bar_y + 19, "H가 L보다 늦게 실행됨", 14, BADGE["red"]["text"])

    # With donation (green left border)
    gy = 570
    gh = 120
    s += card(comp_x, gy, comp_w, gh)
    s += (f'  <rect x="{comp_x}" y="{gy}" width="4" height="{gh}" rx="0" '
          f'fill="{BADGE["green"]["border"]}"/>\n')
    s += text(comp_x + 24, gy + 30, "기부 있을 때 (해결)", 16, TEXT_PRIMARY, "start", "bold")

    bar_y2 = gy + 50
    # L fast
    s += (f'  <rect x="{comp_x + 24}" y="{bar_y2}" width="200" height="{bar_h}" rx="6" '
          f'fill="{BADGE["green"]["bg"]}" stroke="{BADGE["green"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 124, bar_y2 + 19, "L(pri=63) 기부받아 실행", 12, BADGE["green"]["text"], "middle", "bold")
    # H runs next
    s += (f'  <rect x="{comp_x + 240}" y="{bar_y2}" width="300" height="{bar_h}" rx="6" '
          f'fill="{BADGE["blue"]["bg"]}" stroke="{BADGE["blue"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 390, bar_y2 + 19, "H(pri=63) 바로 실행", 12, BADGE["blue"]["text"], "middle", "bold")
    # M last
    s += (f'  <rect x="{comp_x + 556}" y="{bar_y2}" width="200" height="{bar_h}" rx="6" '
          f'fill="{BADGE["orange"]["bg"]}" stroke="{BADGE["orange"]["border"]}" stroke-width="1"/>\n')
    s += text(comp_x + 656, bar_y2 + 19, "M(pri=31) 마지막", 12, BADGE["orange"]["text"], "middle", "bold")
    s += text(comp_x + 780, bar_y2 + 19, "H가 빠르게 실행됨", 14, BADGE["green"]["text"])

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# 06 Struct Thread (1000x950)
# ---------------------------------------------------------------------------
def gen_06():
    W, H = 1000, 950
    s = svg_open(W, H, "struct thread 필드 구성")

    cx = 100
    cw = 800
    row_h = 36
    header_h = 60
    y = 90

    col_type_x = cx + 30
    col_field_x = cx + 260
    col_desc_x = cx + 460

    groups = [
        (None, "기본 필드 (Original)", [
            ("tid_t",              "tid",      "스레드 식별자"),
            ("enum thread_status", "status",   "현재 상태"),
            ("char[16]",           "name",     "스레드 이름"),
            ("int",                "priority", "우선순위 (0-63)"),
            ("struct list_elem",   "elem",     "리스트 요소"),
            ("struct intr_frame",  "tf",       "인터럽트 프레임"),
            ("unsigned",           "magic",    "스택 오버플로 감지"),
        ]),
        (1, "추가 필드", [
            ("int64_t", "wake_tick", "깨어날 틱"),
        ]),
        (3, "추가 필드", [
            ("int",              "original_priority", "기부 전 원래 우선순위"),
            ("struct lock *",    "wait_on_lock",      "대기 중인 락"),
            ("struct list",      "donations",         "기부 목록"),
            ("struct list_elem", "donation_elem",     "기부 리스트 요소"),
        ]),
        (4, "추가 필드", [
            ("int", "nice",       "nice 값 (-20~20)"),
            ("int", "recent_cpu", "최근 CPU 사용량 (고정소수점)"),
        ]),
    ]

    for pn, group_title, fields in groups:
        card_h = header_h + len(fields) * row_h + 20
        s += card(cx, y, cw, card_h)

        # Header row
        if pn is not None:
            s += phase_badge(cx + 30, y + 18, pn)
            bw = len(f"Phase {pn}") * 8 + 24
            s += text(cx + 30 + bw + 12, y + 34, group_title, 20, TEXT_PRIMARY, "start", "bold")
        else:
            s += text(cx + 30, y + 34, group_title, 20, TEXT_PRIMARY, "start", "bold")

        # Column headers
        chy = y + header_h - 6
        s += text(col_type_x, chy, "Type", 14, TEXT_CAPTION, "start", "bold")
        s += text(col_field_x, chy, "Field", 14, TEXT_CAPTION, "start", "bold")
        s += text(col_desc_x, chy, "Description", 14, TEXT_CAPTION, "start", "bold")
        s += divider(cx + 20, chy + 6, cx + cw - 20, chy + 6)

        # Rows
        for i, (ftype, fname, fdesc) in enumerate(fields):
            fy = chy + 12 + i * row_h + row_h // 2
            if i > 0:
                s += divider(cx + 20, fy - row_h // 2, cx + cw - 20, fy - row_h // 2)
            s += text(col_type_x, fy + 5, ftype, 14, TEXT_BODY, ff="mono")
            s += text(col_field_x, fy + 5, fname, 16, TEXT_PRIMARY, "start", "bold")
            s += text(col_desc_x, fy + 5, fdesc, 14, TEXT_CAPTION)

        y += card_h + 20

    s += svg_close()
    return s


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    diagrams = [
        ("01-architecture.svg",      gen_01),
        ("02-phase-dependency.svg",  gen_02),
        ("03-thread-lifecycle.svg",  gen_03),
        ("04-timer-interrupt.svg",   gen_04),
        ("05-priority-donation.svg", gen_05),
        ("06-struct-thread.svg",     gen_06),
    ]

    for filename, gen_func in diagrams:
        path = OUT_DIR / filename
        svg_content = gen_func()
        path.write_text(svg_content, encoding="utf-8")
        print(f"[OK] {path}")

    print(f"\nAll {len(diagrams)} diagrams generated in {OUT_DIR}")


if __name__ == "__main__":
    main()
