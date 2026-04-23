#!/usr/bin/env python3
"""PintOS 프로젝트 문서용 SVG 다이어그램 6종 생성.

디자인 시스템: docs/convention/svg-design-system.md 준수.
"""

from pathlib import Path

# ---------------------------------------------------------------------------
# Design System Constants
# ---------------------------------------------------------------------------
COLORS = {
    "bg":             "#FFFFFF",
    "text_primary":   "#191F28",
    "text_secondary": "#4E5968",
    "text_muted":     "#8B95A1",
    "border":         "#E5E8EB",
    "shadow":         "#F2F4F6",
    "surface":        "#F8FAFC",
}

ACCENT = {
    "blue":   {"bg": "#EFF6FF", "border": "#3B82F6", "text": "#1D4ED8"},
    "green":  {"bg": "#F0FDF4", "border": "#22C55E", "text": "#15803D"},
    "orange": {"bg": "#FFF7ED", "border": "#F97316", "text": "#C2410C"},
    "purple": {"bg": "#FAF5FF", "border": "#A855F7", "text": "#7E22CE"},
    "red":    {"bg": "#FEF2F2", "border": "#EF4444", "text": "#DC2626"},
    "cyan":   {"bg": "#ECFEFF", "border": "#06B6D4", "text": "#0E7490"},
}

# Phase -> accent colour key
PHASE_COLOR = {1: "blue", 2: "green", 3: "orange", 4: "purple"}
PHASE_LABEL = {
    1: "Phase 1: Alarm Clock",
    2: "Phase 2: Priority Scheduling",
    3: "Phase 3: Priority Donation",
    4: "Phase 4: MLFQS",
}

FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"
FONT_SIZE = {"title": 32, "heading": 22, "body": 18, "label": 15, "caption": 13}
CARD_RX = 12
CARD_RX_LARGE = 20
SHADOW_OFFSET = 3
STROKE_W = 2
PADDING = 20
MARGIN = 60

OUT_DIR = Path(__file__).resolve().parent.parent / "docs" / "pintos" / "img"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def phase(p):
    """Return accent dict for a phase number."""
    return ACCENT[PHASE_COLOR[p]]


def svg_header(w, h, title_text):
    return (
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg"\n'
        f'     width="{w}" height="{h}" viewBox="0 0 {w} {h}"\n'
        f'     font-family="{FONT}">\n'
        f'  <rect width="{w}" height="{h}" fill="{COLORS["bg"]}"/>\n'
        f'  <text x="{w // 2}" y="52" text-anchor="middle"\n'
        f'        font-size="{FONT_SIZE["title"]}" font-weight="bold" fill="{COLORS["text_primary"]}">\n'
        f'    {title_text}\n'
        f'  </text>\n'
    )


def svg_footer():
    return "</svg>\n"


def arrow_defs(marker_id="arrowhead", color="#4E5968"):
    return (
        f'  <defs>\n'
        f'    <marker id="{marker_id}" markerWidth="16" markerHeight="11"\n'
        f'            refX="14" refY="5.5" orient="auto">\n'
        f'      <polygon points="0 0, 16 5.5, 0 11" fill="{color}" />\n'
        f'    </marker>\n'
        f'  </defs>\n'
    )


def card(x, y, w, h, fill="#FFFFFF", border="#E5E8EB", rx=CARD_RX, sw=STROKE_W):
    shadow = (
        f'  <rect x="{x + SHADOW_OFFSET}" y="{y + SHADOW_OFFSET}" '
        f'width="{w}" height="{h}" rx="{rx}" fill="{COLORS["shadow"]}" />\n'
    )
    main = (
        f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" '
        f'rx="{rx}" fill="{fill}" stroke="{border}" stroke-width="{sw}" />\n'
    )
    return shadow + main


def txt(x, y, content, size=FONT_SIZE["body"], fill=COLORS["text_primary"],
        anchor="start", weight="normal"):
    return (
        f'  <text x="{x}" y="{y}" font-size="{size}" fill="{fill}" '
        f'text-anchor="{anchor}" font-weight="{weight}">{content}</text>\n'
    )


def badge(x, y, label, bg_color, text_color="#FFFFFF", w=80, h=26):
    rx = h // 2
    return (
        f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" '
        f'rx="{rx}" fill="{bg_color}" />\n'
        f'  <text x="{x + w // 2}" y="{y + h // 2 + 5}" '
        f'font-size="{FONT_SIZE["caption"]}" fill="{text_color}" '
        f'text-anchor="middle" font-weight="bold">{label}</text>\n'
    )


def arrow_line(x1, y1, x2, y2, mid="arrowhead", color="#4E5968", sw=STROKE_W):
    return (
        f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="{sw}" '
        f'marker-end="url(#{mid})" />\n'
    )


def arrow_path(d, mid="arrowhead", color="#4E5968", sw=STROKE_W):
    return (
        f'  <path d="{d}" fill="none" stroke="{color}" '
        f'stroke-width="{sw}" marker-end="url(#{mid})" />\n'
    )


def separator(x1, y1, x2, y2, color=COLORS["border"], opacity="0.4"):
    return (
        f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="1" opacity="{opacity}" />\n'
    )


# ---------------------------------------------------------------------------
# 1. Architecture  (1200 x 850)
# ---------------------------------------------------------------------------
def gen_architecture():
    W, H = 1200, 850
    s = svg_header(W, H, "PintOS Threads -- 모듈 구조")
    s += arrow_defs("arr")

    # --- Three module cards in a row ---
    cw, ch = 320, 280
    gap = 30
    total_w = cw * 3 + gap * 2
    start_x = (W - total_w) // 2
    cy = 90

    modules = [
        ("timer.c", 1, ["timer_sleep()", "timer_interrupt()", "thread_awake()", "mlfqs_recalc()"]),
        ("thread.c", 2, ["thread_create()", "thread_unblock()", "thread_yield()", "thread_set_priority()"]),
        ("synch.c", 3, ["lock_acquire()", "lock_release()", "sema_down()", "sema_up()"]),
    ]

    card_positions = []
    for i, (name, ph, funcs) in enumerate(modules):
        cx = start_x + i * (cw + gap)
        card_positions.append((cx, cy, cw, ch))
        p = phase(ph)
        s += card(cx, cy, cw, ch, fill=p["bg"], border=p["border"])
        # header
        s += txt(cx + cw // 2, cy + 38, name, FONT_SIZE["heading"], p["text"], "middle", "bold")
        # divider
        s += separator(cx + PADDING, cy + 52, cx + cw - PADDING, cy + 52, p["border"], "0.5")
        # functions
        for j, fn in enumerate(funcs):
            s += txt(cx + 30, cy + 88 + j * 32, fn, FONT_SIZE["body"])
        # phase badge
        s += badge(cx + cw - 100, cy + ch - 40, f"Phase {ph}", p["border"], "#FFFFFF")

    # --- Arrows between cards ---
    # timer.c -> thread.c
    ax1 = card_positions[0][0] + cw + 5
    ax2 = card_positions[1][0] - 5
    ay = cy + ch // 2
    s += arrow_line(ax1, ay, ax2, ay, "arr")
    s += txt((ax1 + ax2) // 2, ay - 10, "호출", FONT_SIZE["label"], COLORS["text_secondary"], "middle")

    # thread.c -> synch.c
    bx1 = card_positions[1][0] + cw + 5
    bx2 = card_positions[2][0] - 5
    s += arrow_line(bx1, ay, bx2, ay, "arr")
    s += txt((bx1 + bx2) // 2, ay - 10, "호출", FONT_SIZE["label"], COLORS["text_secondary"], "middle")

    # synch.c -> thread.c (return arrow, slightly lower)
    s += arrow_path(
        f"M {bx2},{ay + 30} C {bx2 - 10},{ay + 50} {bx1 + 10},{ay + 50} {bx1},{ay + 30}",
        "arr"
    )
    s += txt((bx1 + bx2) // 2, ay + 65, "콜백", FONT_SIZE["label"], COLORS["text_secondary"], "middle")

    # --- Phase 4 full-width card ---
    p4 = phase(4)
    p4y = cy + ch + 40
    p4w = total_w
    p4h = 200
    p4x = start_x
    s += card(p4x, p4y, p4w, p4h, fill=p4["bg"], border=p4["border"], rx=CARD_RX_LARGE)
    s += badge(p4x + PADDING, p4y + 18, "Phase 4", p4["border"])
    s += txt(p4x + PADDING + 95, p4y + 38, "MLFQS -- 전체 모듈에 걸친 변경",
             FONT_SIZE["heading"], p4["text"], "start", "bold")

    cols = [
        ("timer.c", ["mlfqs_increment()", "mlfqs_recalc()", "mlfqs_priority()"]),
        ("thread.c", ["thread_set_nice()", "thread_get_nice()", "thread_get_recent_cpu()"]),
        ("synch.c", ["priority donation 비활성화", "(MLFQS 모드)"]),
    ]
    col_w = (p4w - PADDING * 4) // 3
    for i, (col_title, items) in enumerate(cols):
        col_x = p4x + PADDING * 2 + i * (col_w + PADDING)
        s += txt(col_x, p4y + 80, col_title, FONT_SIZE["body"], p4["text"], "start", "bold")
        for j, item in enumerate(items):
            s += txt(col_x, p4y + 108 + j * 27, item, FONT_SIZE["label"], COLORS["text_secondary"])

    # --- Legend ---
    ly = H - 80
    s += txt(MARGIN, ly, "범례", FONT_SIZE["body"], COLORS["text_primary"], "start", "bold")
    for i, (pn, color_key) in enumerate(PHASE_COLOR.items()):
        lx = MARGIN + i * 270
        p = ACCENT[color_key]
        s += (f'  <rect x="{lx}" y="{ly + 14}" width="20" height="20" rx="4" '
              f'fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="{STROKE_W}" />\n')
        s += txt(lx + 30, ly + 30, PHASE_LABEL[pn], FONT_SIZE["label"], p["text"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 2. Phase Dependency  (1200 x 950)
# ---------------------------------------------------------------------------
def gen_phase_dependency():
    W, H = 1200, 950
    s = svg_header(W, H, "Phase 의존 관계")
    s += arrow_defs("arr")

    phases_data = [
        (1, "Alarm Clock", "timer_sleep 재구현, sleep_list 도입", "busy-wait 제거"),
        (2, "Priority Scheduling", "ready_list 우선순위 정렬, 선점 체크", "높은 우선순위 먼저 실행"),
        (3, "Priority Donation", "lock에서 우선순위 기부/회수", "우선순위 역전 방지"),
        (4, "MLFQS", "nice, recent_cpu, load_avg로 자동 계산", "공정한 CPU 분배"),
    ]

    cw, ch = 800, 140
    cx = (W - cw) // 2
    start_y = 100
    gap_y = 60  # gap between cards (arrow space)

    for idx, (pn, title, desc, output) in enumerate(phases_data):
        cy = start_y + idx * (ch + gap_y)
        p = phase(pn)
        s += card(cx, cy, cw, ch, fill=p["bg"], border=p["border"], rx=CARD_RX_LARGE)

        # Phase badge
        s += badge(cx + PADDING, cy + 18, f"Phase {pn}", p["border"])
        # Title
        s += txt(cx + PADDING + 95, cy + 38, title, FONT_SIZE["heading"], p["text"], "start", "bold")
        # Description
        s += txt(cx + PADDING + 20, cy + 75, desc, FONT_SIZE["body"])
        # Output tag
        tag_w = len(output) * 11 + PADDING * 2
        tag_x = cx + PADDING + 20
        tag_y = cy + 95
        s += (f'  <rect x="{tag_x}" y="{tag_y}" width="{tag_w}" height="28" '
              f'rx="14" fill="{p["bg"]}" stroke="{p["border"]}" stroke-width="1" />\n')
        s += txt(tag_x + tag_w // 2, tag_y + 19, output, FONT_SIZE["label"], p["text"], "middle", "bold")

        # Arrow to next phase
        if idx < 3:
            arrow_y1 = cy + ch + 5
            arrow_y2 = cy + ch + gap_y - 5
            s += arrow_line(W // 2, arrow_y1, W // 2, arrow_y2, "arr", p["border"], 3)

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 3. Thread Lifecycle  (1200 x 800)
# ---------------------------------------------------------------------------
def gen_thread_lifecycle():
    W, H = 1200, 800
    s = svg_header(W, H, "스레드 생명 주기")
    s += arrow_defs("arr")

    # State positions (center x, center y)
    sw, sh = 220, 100
    states = {
        "BLOCKED":  (180,  420, phase(1)),
        "READY":    (600,  180, phase(2)),
        "RUNNING":  (1020, 420, phase(2)),
        "DYING":    (600,  660, phase(4)),
    }

    for name, (cx, cy, p) in states.items():
        x, y = cx - sw // 2, cy - sh // 2
        s += card(x, y, sw, sh, fill=p["bg"], border=p["border"], rx=CARD_RX_LARGE)
        s += txt(cx, cy + 8, name, FONT_SIZE["heading"], p["text"], "middle", "bold")

    # --- Transitions ---
    # Helper for phase badge near a point
    def phase_badge_at(px, py, pn):
        p = phase(pn)
        return badge(px - 40, py, f"Phase {pn}", p["border"], "#FFFFFF")

    # create -> READY
    s += arrow_line(380, 100, 500, 145, "arr")
    s += txt(350, 95, "thread_create()", FONT_SIZE["label"], COLORS["text_secondary"])

    # BLOCKED -> READY
    bx1, by1 = 290, 385  # right-top of BLOCKED
    bx2, by2 = 490, 215  # left-bottom of READY
    s += arrow_line(bx1, by1, bx2, by2, "arr")
    mid_x, mid_y = (bx1 + bx2) // 2, (by1 + by2) // 2
    s += txt(mid_x - 80, mid_y - 15, "thread_unblock()", FONT_SIZE["label"], COLORS["text_secondary"])
    s += txt(mid_x - 80, mid_y + 8, "sema_up()", FONT_SIZE["label"], COLORS["text_secondary"])
    s += phase_badge_at(mid_x - 80, mid_y + 15, 1)

    # READY -> RUNNING
    rx1, ry1 = 710, 215  # right-bottom of READY
    rx2, ry2 = 910, 385  # left-top of RUNNING
    s += arrow_line(rx1, ry1, rx2, ry2, "arr")
    mid_x2, mid_y2 = (rx1 + rx2) // 2, (ry1 + ry2) // 2
    s += txt(mid_x2 + 15, mid_y2 - 5, "schedule()", FONT_SIZE["label"], COLORS["text_secondary"])
    s += phase_badge_at(mid_x2 + 15, mid_y2 + 5, 2)

    # RUNNING -> BLOCKED
    s += arrow_path(
        f"M {910},{ry2 + 30} C {700},{ry2 + 120} {400},{by1 + 120} {290},{by1 + 30}",
        "arr"
    )
    s += txt(550, 530, "sema_down() / lock_acquire()", FONT_SIZE["label"], COLORS["text_secondary"])
    s += phase_badge_at(550, 540, 3)

    # RUNNING -> READY (yield)
    s += arrow_path(
        f"M {1020 - sw // 2},{states['RUNNING'][1] - sh // 2 - 5} "
        f"C {900},{180} {750},{130} {600 + sw // 2},{states['READY'][1] - 5}",
        "arr"
    )
    s += txt(830, 135, "thread_yield()", FONT_SIZE["label"], COLORS["text_secondary"])
    s += phase_badge_at(830, 145, 2)

    # RUNNING -> DYING
    dx1, dy1 = 1020, 420 + sh // 2 + 5  # bottom of RUNNING
    dx2, dy2 = 600 + sw // 2, 660 - sh // 2 - 5  # right of DYING
    s += arrow_line(dx1, dy1, dx2 + 20, dy2 + 30, "arr")
    s += txt(850, 600, "thread_exit()", FONT_SIZE["label"], COLORS["text_secondary"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 4. Timer Interrupt  (1200 x 900)
# ---------------------------------------------------------------------------
def gen_timer_interrupt():
    W, H = 1200, 900
    s = svg_header(W, H, "timer_interrupt() 처리 흐름")
    s += arrow_defs("arr")

    # Root card
    root_w, root_h = 400, 60
    root_x = (W - root_w) // 2
    root_y = 90
    s += card(root_x, root_y, root_w, root_h, fill=COLORS["surface"], border=COLORS["border"])
    s += txt(root_x + root_w // 2, root_y + 38, "timer_interrupt()",
             FONT_SIZE["heading"], COLORS["text_primary"], "middle", "bold")

    # Level 1 children
    child_x = 120
    child_w = W - child_x * 2

    # Child 1: ticks++
    c1y = 190
    c1h = 55
    s += card(child_x, c1y, child_w, c1h, fill=COLORS["surface"], border=COLORS["border"])
    s += txt(child_x + PADDING + 10, c1y + 35, "ticks++", FONT_SIZE["body"])
    s += arrow_line(W // 2, root_y + root_h + 5, W // 2, c1y - 5, "arr")

    # Child 2: thread_tick()
    c2y = c1y + c1h + 30
    c2h = 55
    s += card(child_x, c2y, child_w, c2h, fill=COLORS["surface"], border=COLORS["border"])
    s += txt(child_x + PADDING + 10, c2y + 35, "thread_tick() -- 타임 슬라이스 소진 시 선점 플래그 설정",
             FONT_SIZE["body"])
    s += arrow_line(W // 2, c1y + c1h + 5, W // 2, c2y - 5, "arr")

    # Child 3: Phase 1 thread_awake
    p1 = phase(1)
    c3y = c2y + c2h + 30
    c3h = 80
    s += card(child_x, c3y, child_w, c3h, fill=p1["bg"], border=p1["border"])
    s += badge(child_x + PADDING, c3y + 14, "Phase 1", p1["border"])
    s += txt(child_x + PADDING + 95, c3y + 34,
             "thread_awake() -- sleep 스레드 깨우기",
             FONT_SIZE["body"], p1["text"], "start", "bold")
    s += txt(child_x + PADDING + 95, c3y + 62,
             "sleep_list 순회, wake_tick <= ticks 인 스레드 unblock",
             FONT_SIZE["label"], COLORS["text_secondary"])
    s += arrow_line(W // 2, c2y + c2h + 5, W // 2, c3y - 5, "arr")

    # Child 4: Phase 4 MLFQS large card
    p4 = phase(4)
    c4y = c3y + c3h + 40
    c4h = H - c4y - MARGIN
    s += card(child_x, c4y, child_w, c4h, fill=p4["bg"], border=p4["border"], rx=CARD_RX_LARGE)
    s += badge(child_x + PADDING, c4y + 18, "Phase 4", p4["border"])
    s += txt(child_x + PADDING + 95, c4y + 38, "MLFQS 연산",
             FONT_SIZE["heading"], p4["text"], "start", "bold")
    s += arrow_line(W // 2, c3y + c3h + 5, W // 2, c4y - 5, "arr")

    # Sub-cards inside MLFQS
    sub_x = child_x + 40
    sub_w = child_w - 80
    sub_items = [
        ("매 틱", ["recent_cpu += 1 (현재 스레드)"], 80),
        ("매 TIMER_FREQ (1초)", ["load_avg 재계산", "recent_cpu 전체 스레드 재계산"], 80 + 90),
        ("매 4틱", ["priority 재계산 (전체 스레드)"], 80 + 90 + 110),
    ]
    for timing, descs, offset_y in sub_items:
        sub_y = c4y + offset_y
        sub_h = 30 + len(descs) * 27 + 10
        s += card(sub_x, sub_y, sub_w, sub_h, fill="#FFFFFF", border=p4["border"])
        # timing badge
        bw = len(timing) * 12 + 24
        s += badge(sub_x + 15, sub_y + 12, timing, p4["bg"])
        # override badge text color since light bg
        s += (f'  <rect x="{sub_x + 15}" y="{sub_y + 12}" width="{bw}" height="26" '
              f'rx="13" fill="{p4["bg"]}" stroke="{p4["border"]}" stroke-width="1" />\n')
        s += txt(sub_x + 15 + bw // 2, sub_y + 30, timing,
                 FONT_SIZE["caption"], p4["text"], "middle", "bold")
        for j, d in enumerate(descs):
            s += txt(sub_x + 25 + bw + 15, sub_y + 30 + j * 27, d, FONT_SIZE["body"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 5. Priority Donation  (1200 x 850)
# ---------------------------------------------------------------------------
def gen_priority_donation():
    W, H = 1200, 850
    s = svg_header(W, H, "Priority Donation 타임라인")
    s += arrow_defs("arr")

    p3 = phase(3)

    # Timeline axis
    tl_y = 310
    tl_x1, tl_x2 = MARGIN + 20, W - MARGIN - 20
    s += (f'  <line x1="{tl_x1}" y1="{tl_y}" x2="{tl_x2}" y2="{tl_y}" '
          f'stroke="{COLORS["border"]}" stroke-width="3" />\n')

    events = [
        ("T1", 180,  "L(pri=1)이",     "Lock A 획득",     "pri=1"),
        ("T2", 440,  "H(pri=63)이",    "Lock A 요청",     "63 기부"),
        ("T3", 700,  "L(pri=63) 완료", "Lock A 해제",     "pri=1 복원"),
        ("T4", 960,  "H(pri=63)",      "Lock A 획득",     "pri=63"),
    ]

    for label, x, line1, line2, pri_text in events:
        # dot
        s += f'  <circle cx="{x}" cy="{tl_y}" r="8" fill="{p3["border"]}" />\n'
        # time label below dot
        s += txt(x, tl_y + 30, label, FONT_SIZE["heading"], p3["text"], "middle", "bold")

        # event card above
        ec_w, ec_h = 180, 70
        ec_x = x - ec_w // 2
        ec_y = tl_y - 95
        s += card(ec_x, ec_y, ec_w, ec_h, fill=p3["bg"], border=p3["border"])
        s += txt(x, ec_y + 30, line1, FONT_SIZE["label"], COLORS["text_primary"], "middle")
        s += txt(x, ec_y + 52, line2, FONT_SIZE["label"], COLORS["text_primary"], "middle")

        # connector line from card to dot
        s += (f'  <line x1="{x}" y1="{ec_y + ec_h}" x2="{x}" y2="{tl_y - 8}" '
              f'stroke="{p3["border"]}" stroke-width="1" stroke-dasharray="4,3" />\n')

        # priority badge below timeline
        s += badge(x - 50, tl_y + 45, pri_text, p3["border"], "#FFFFFF", 100)

    # --- Comparison section ---
    comp_y = 440
    s += txt(W // 2, comp_y, "비교", FONT_SIZE["heading"], COLORS["text_primary"], "middle", "bold")

    # Without donation (red)
    red = ACCENT["red"]
    r_y = comp_y + 20
    r_h = 150
    s += card(MARGIN, r_y, W - MARGIN * 2, r_h, fill=red["bg"], border=red["border"])
    s += txt(MARGIN + PADDING + 10, r_y + 32, "기부 없을 때 (우선순위 역전 발생)",
             FONT_SIZE["body"], red["text"], "start", "bold")

    # timeline bars
    bar_y = r_y + 55
    bar_h = 32
    bar_rx = 6
    # L running long
    s += (f'  <rect x="{MARGIN + 30}" y="{bar_y}" width="320" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#FECACA" stroke="{red["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 190, bar_y + 22, "L(pri=1) 실행 중 -- Lock A 보유",
             FONT_SIZE["caption"], red["text"], "middle")
    # M interrupts
    s += (f'  <rect x="{MARGIN + 370}" y="{bar_y}" width="120" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#FED7AA" stroke="{ACCENT["orange"]["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 430, bar_y + 22, "M(pri=31)",
             FONT_SIZE["caption"], ACCENT["orange"]["text"], "middle")
    # H waiting
    blue = ACCENT["blue"]
    s += (f'  <rect x="{MARGIN + 510}" y="{bar_y}" width="340" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#DBEAFE" stroke="{blue["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 680, bar_y + 22, "H(pri=63) 대기 -- 역전 발생!",
             FONT_SIZE["caption"], blue["text"], "middle")
    s += txt(MARGIN + 900, bar_y + 50, "H가 L보다 늦게 실행됨",
             FONT_SIZE["label"], red["text"])

    # With donation (green)
    grn = ACCENT["green"]
    g_y = r_y + r_h + 25
    g_h = 150
    s += card(MARGIN, g_y, W - MARGIN * 2, g_h, fill=grn["bg"], border=grn["border"])
    s += txt(MARGIN + PADDING + 10, g_y + 32, "기부 있을 때 (해결)",
             FONT_SIZE["body"], grn["text"], "start", "bold")

    bar_y2 = g_y + 55
    # L runs fast with donated priority
    s += (f'  <rect x="{MARGIN + 30}" y="{bar_y2}" width="220" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#BBF7D0" stroke="{grn["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 140, bar_y2 + 22, "L(pri=63) 기부받아 실행",
             FONT_SIZE["caption"], grn["text"], "middle")
    # H runs right after
    s += (f'  <rect x="{MARGIN + 270}" y="{bar_y2}" width="280" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#DBEAFE" stroke="{blue["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 410, bar_y2 + 22, "H(pri=63) 바로 실행",
             FONT_SIZE["caption"], blue["text"], "middle")
    # M runs last
    s += (f'  <rect x="{MARGIN + 570}" y="{bar_y2}" width="150" height="{bar_h}" '
          f'rx="{bar_rx}" fill="#FED7AA" stroke="{ACCENT["orange"]["border"]}" stroke-width="1" />\n')
    s += txt(MARGIN + 645, bar_y2 + 22, "M(pri=31)",
             FONT_SIZE["caption"], ACCENT["orange"]["text"], "middle")
    s += txt(MARGIN + 770, bar_y2 + 50, "H가 빠르게 실행됨",
             FONT_SIZE["label"], grn["text"])

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# 6. Struct Thread  (1200 x 1000)
# ---------------------------------------------------------------------------
def gen_struct_thread():
    W, H = 1200, 1000
    s = svg_header(W, H, "struct thread 필드 구성")

    cx = 160
    cw = W - cx * 2  # 880
    row_h = 44
    header_h = 55
    y = 90

    groups = [
        (None, "기본 필드 (Original)", [
            ("tid_t",             "tid",    "스레드 식별자"),
            ("enum thread_status","status", "현재 상태"),
            ("char[16]",         "name",   "스레드 이름"),
            ("int",              "priority","우선순위 (0-63)"),
            ("struct list_elem", "elem",   "리스트 요소"),
            ("struct intr_frame","tf",     "인터럽트 프레임"),
            ("unsigned",         "magic",  "스택 오버플로 감지"),
        ]),
        (1, "Phase 1 추가 필드", [
            ("int64_t", "wake_tick", "깨어날 틱"),
        ]),
        (3, "Phase 3 추가 필드", [
            ("int",              "original_priority", "기부 전 원래 우선순위"),
            ("struct lock*",     "wait_on_lock",      "대기 중인 락"),
            ("struct list",      "donations",         "기부 목록"),
            ("struct list_elem", "donation_elem",     "기부 리스트 요소"),
        ]),
        (4, "Phase 4 추가 필드", [
            ("int", "nice",       "nice 값 (-20~20)"),
            ("int", "recent_cpu", "최근 CPU 사용량 (고정소수점)"),
        ]),
    ]

    col_type_x = cx + 30
    col_name_x = cx + 240
    col_desc_x = cx + 460

    for ph, group_title, fields in groups:
        if ph:
            p = phase(ph)
            bg, border, title_color = p["bg"], p["border"], p["text"]
        else:
            bg = COLORS["surface"]
            border = COLORS["border"]
            title_color = COLORS["text_primary"]

        group_h = header_h + len(fields) * row_h + PADDING
        s += card(cx, y, cw, group_h, fill=bg, border=border)

        # Group header
        if ph:
            s += badge(cx + PADDING, y + 15, f"Phase {ph}", border)
            s += txt(cx + PADDING + 95, y + 35, group_title,
                     FONT_SIZE["body"], title_color, "start", "bold")
        else:
            s += txt(cx + PADDING, y + 35, group_title,
                     FONT_SIZE["body"], title_color, "start", "bold")

        # Column headers
        hy = y + header_h
        s += txt(col_type_x, hy, "Type", FONT_SIZE["label"], COLORS["text_secondary"], "start", "bold")
        s += txt(col_name_x, hy, "Field", FONT_SIZE["label"], COLORS["text_secondary"], "start", "bold")
        s += txt(col_desc_x, hy, "Description", FONT_SIZE["label"], COLORS["text_secondary"], "start", "bold")

        for i, (ftype, fname, fdesc) in enumerate(fields):
            fy = hy + 20 + i * row_h
            if i > 0:
                s += separator(cx + PADDING, fy - 10, cx + cw - PADDING, fy - 10, border)
            s += txt(col_type_x, fy + 14, ftype, FONT_SIZE["label"], COLORS["text_secondary"])
            s += txt(col_name_x, fy + 14, fname, FONT_SIZE["label"], COLORS["text_primary"], "start", "bold")
            s += txt(col_desc_x, fy + 14, fdesc, FONT_SIZE["label"], COLORS["text_secondary"])

        y += group_h + 20

    s += svg_footer()
    return s


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    diagrams = [
        ("01-architecture.svg",     gen_architecture),
        ("02-phase-dependency.svg", gen_phase_dependency),
        ("03-thread-lifecycle.svg", gen_thread_lifecycle),
        ("04-timer-interrupt.svg",  gen_timer_interrupt),
        ("05-priority-donation.svg",gen_priority_donation),
        ("06-struct-thread.svg",    gen_struct_thread),
    ]

    for filename, gen_func in diagrams:
        path = OUT_DIR / filename
        svg_content = gen_func()
        path.write_text(svg_content, encoding="utf-8")
        print(f"[OK] {path}")

    print(f"\nAll {len(diagrams)} diagrams generated in {OUT_DIR}")


if __name__ == "__main__":
    main()
