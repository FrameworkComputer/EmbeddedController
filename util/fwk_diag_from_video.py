#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Decode the Framework laptop boot diagnostic LED blink code from a video.

When a Framework laptop fails to boot within DIAGNOSTIC_TIMER_SECONDS the EC
blinks the battery/side LED (see
zephyr/program/framework/src/diagnostics_laptop.c)
with 200 ms on / 200 ms off per symbol:

    1 x WHITE              start marker
   12 x RED/GREEN          hardware diagnostic bits 1..12 (red = fault)
    1 x AMBER              separator
   16 x BLUE/GREEN         last port80 code, LSB first (blue = 1)

The sequence is shown once. This script locates the LED in the video,
segments it into blinks, classifies the color of each and fits the result
onto that 30 symbol template, so recordings that start late or end early
still decode whatever is visible.

Requires ffmpeg/ffprobe and numpy.
"""

import argparse
import colorsys
import json
import subprocess
import sys

import numpy as np

DIAG_PERIOD_S = 0.4

HW_BITS_COMMON = {
    1: "No battery",
    2: "HW PGOOD 3V5V",
    3: "VCCIN AUX VR (power bad, forced G3)",
    4: "SLP_S4",
    5: "HW PGOOD VR",
    10: "No S0",
    11: "No DDR",
    12: "No eDP",
}
HW_BITS_PLATFORM = {
    "fw13": {
        6: "Touchpad",
        7: "Audio daughterboard",
        8: "Thermal sensor",
        9: "No fan",
    },
    "fw12": {
        6: "Input cover",
        7: "Audio daughterboard",
        8: "Camera module",
        9: "No fan",
    },
    "fw16": {
        6: "Input module fault",
        7: "No left fan",
        8: "No right fan",
        9: "GPU module fault",
    },
}

# Template slots: what color each position of the sequence may have.
SLOT_WHITE = 0
SLOT_HW_FIRST = 1
SLOT_HW_LAST = 12
SLOT_AMBER = 13
SLOT_BIOS_FIRST = 14
SLOT_BIOS_LAST = 29
NUM_SLOTS = 30


def slot_allowed(slot):
    """Colors allowed at a template slot."""
    if slot == SLOT_WHITE:
        return {"white"}
    if slot <= SLOT_HW_LAST:
        return {"red", "green"}
    if slot == SLOT_AMBER:
        return {"amber"}
    return {"blue", "green"}


def probe_video(path):
    out = subprocess.check_output(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,avg_frame_rate:stream_side_data=rotation",
            "-of",
            "json",
            path,
        ]
    )
    stream = json.loads(out)["streams"][0]
    num, den = stream["avg_frame_rate"].split("/")
    width, height = stream["width"], stream["height"]
    # ffmpeg autorotates on decode, so swap dimensions for portrait video
    for side in stream.get("side_data_list", []):
        if abs(int(side.get("rotation", 0))) in (90, 270):
            width, height = height, width
    return width, height, float(num) / float(den)


def read_frames(path, width, height, vf=None):
    """Yield decoded frames as HxWx3 uint8 arrays."""
    cmd = ["ffmpeg", "-v", "error", "-i", path]
    if vf:
        cmd += ["-vf", vf]
    cmd += ["-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = width * height * 3
    try:
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            yield np.frombuffer(buf, np.uint8).reshape(height, width, 3)
    finally:
        proc.stdout.close()
        proc.wait()


def led_strength(px):
    """Per pixel "is a lit LED" score, 0 for pixels that can't be the LED.

    A lit LED is either strongly saturated and bright, or clipped to white in
    every channel. Bright but pastel surfaces (e.g. colored keycaps) don't
    clip on all channels at exposures where the LED is visible.
    """
    mx = px.max(-1)
    mn = px.min(-1)
    score = (mx - mn) * mx
    score[(mx <= 200) | (mx - mn <= 100)] = 0
    score[mn > 240] = 255 * 255
    return score


def box_sum(a, half):
    """Sum of a over a (2*half+1)^2 box around each element."""
    ii = np.zeros((a.shape[0] + 1, a.shape[1] + 1), np.int32)
    ii[1:, 1:] = a.cumsum(0).cumsum(1)
    ys, xs = np.arange(a.shape[0]), np.arange(a.shape[1])
    y0 = np.clip(ys - half, 0, a.shape[0])[:, None]
    y1 = np.clip(ys + half + 1, 0, a.shape[0])[:, None]
    x0 = np.clip(xs - half, 0, a.shape[1])[None, :]
    x1 = np.clip(xs + half + 1, 0, a.shape[1])[None, :]
    return ii[y1, x1] - ii[y0, x1] - ii[y1, x0] + ii[y0, x0]


def pick_compact(score, radius):
    """Return the strongest LED-like spot that is a small isolated blob.

    Large bright areas (an overexposed window, a colorful label) also pass
    led_strength(); they are rejected because they extend past the LED
    radius.
    """
    mask = (score > 0).astype(np.int32)
    inner = box_sum(mask, radius)
    outer = box_sum(mask, int(2.5 * radius))
    score = np.where(outer - inner <= 0.1 * inner, score, 0)
    if score.max() == 0:
        return None
    y, x = np.unravel_index(score.argmax(), score.shape)
    return int(y), int(x)


def find_led(frame, cx, cy, half, radius):
    """Find the strongest LED-like blob in a window around (cx, cy).

    Returns ((x, y), mask pixel count, pixels around the blob) or None.
    """
    height, width = frame.shape[:2]
    x0, x1 = max(0, cx - half), min(width, cx + half)
    y0, y1 = max(0, cy - half), min(height, cy + half)
    roi = frame[y0:y1, x0:x1].astype(np.int32)
    score = led_strength(roi)
    spot = pick_compact(score, radius)
    if spot is None:
        return None
    y, x = spot
    ys, xs = np.ogrid[: roi.shape[0], : roi.shape[1]]
    near = (ys - y) ** 2 + (xs - x) ** 2 <= radius**2
    count = int(np.count_nonzero(near & (score > 0)))
    # Include the dimmer halo, it carries the hue when the core clips
    pixels = roi[near & (roi.max(2) > 120)]
    return (x + x0, y + y0), count, pixels


def global_search(frame, radius):
    """Find the LED anywhere in the frame, coarse first then refined."""
    step = 4
    score = led_strength(frame[::step, ::step].astype(np.int32))
    spot = pick_compact(score, max(2, radius // step))
    if spot is None:
        return None
    y, x = spot
    return find_led(frame, x * step, y * step, 3 * radius, radius)


def classify_pixels(pixels):
    """Map the lit pixels of one frame to a color name."""
    mx = pixels.max(1)
    sat = mx - pixels.min(1)
    colored = pixels[sat > 60]
    # A white LED clips to white in the core and has little colored fringe
    if len(colored) < 0.3 * len(pixels):
        return "white", None
    r, g, b = colored.sum(0) / colored.sum(0).max()
    hue = colorsys.rgb_to_hsv(r, g, b)[0] * 360
    if hue < 45 or hue >= 330:
        return "warm", hue
    if hue < 75:
        return "yellow", hue
    if hue < 180:
        return "green", hue
    if hue < 270:
        return "blue", hue
    return "magenta", hue


def analyze_frames(path, width, height, fps, center, roi_half, on_pixels):
    """Track the LED and classify each frame, None when it is off.

    The search window follows the LED to cope with a handheld camera. If the
    LED isn't seen for a while (camera panned away) search the whole frame.
    """
    radius = max(8, height // 40)
    lost = 0
    results = []
    for frame in read_frames(path, width, height):
        found = None
        if center is not None:
            found = find_led(frame, *center, roi_half, radius)
        if (found is None or found[1] < on_pixels) and (
            center is None or lost > fps
        ):
            found = global_search(frame, radius)
        if found is None or found[1] < on_pixels:
            lost += 1
            results.append(None)
            continue
        lost = 0
        center, _, pixels = found
        results.append(classify_pixels(pixels))
    return results


def make_blink(frames, start, end, fps):
    colors = [frames[j][0] for j in range(start, end)]
    hues = [frames[j][1] for j in range(start, end) if frames[j][1] is not None]
    return {
        "start": start / fps,
        "duration": (end - start) / fps,
        "color": max(set(colors), key=colors.count),
        "hue": float(np.median(hues)) if hues else None,
        "votes": {c: colors.count(c) for c in set(colors)},
    }


def split_by_color(frames, start, end, min_frames):
    """Split one lit run where the color changes without the LED going dark.

    Color groups shorter than min_frames are treated as transitions (e.g. a
    frame catching green fading into amber) and dropped.
    """
    groups = []
    for i in range(start, end):
        if groups and frames[groups[-1][1] - 1][0] == frames[i][0]:
            groups[-1][1] = i + 1
        else:
            groups.append([i, i + 1])
    groups = [g for g in groups if g[1] - g[0] >= min_frames]
    return groups or [[start, end]]


def segment_blinks(frames, fps):
    """Group consecutive lit frames into blinks."""
    min_frames = max(2, int(fps * 0.067))
    blinks = []
    start = None
    for i, res in enumerate(frames + [None]):
        if res is not None and start is None:
            start = i
        elif res is None and start is not None:
            for s, e in split_by_color(frames, start, i, min_frames):
                blinks.append(make_blink(frames, s, e, fps))
            start = None
    return blinks


def estimate_period(blinks):
    diffs = np.diff([b["start"] for b in blinks])
    near = diffs[abs(diffs - DIAG_PERIOD_S) < DIAG_PERIOD_S / 4]
    return float(np.median(near)) if len(near) else DIAG_PERIOD_S


def assign_slots(blinks, period):
    """Put blinks on the diagnostic time grid and keep the main sequence.

    Blinks off the grid (e.g. the tail of a different LED pattern that was
    interrupted when the diagnostic started) are dropped. Of the remaining
    ones, the largest group without long gaps is returned, with "pos" set
    to its index on the grid.
    """
    starts = np.array([b["start"] for b in blinks])
    # Circular mean of the start phases
    angles = 2 * np.pi * starts / period
    phase = np.angle(np.exp(1j * angles).mean()) / (2 * np.pi) * period
    groups = []
    for b in blinks:
        k = (b["start"] - phase) / period
        if abs(k - round(k)) > 0.25:
            continue
        b["pos"] = int(round(k))
        if groups and b["pos"] == groups[-1][-1]["pos"]:
            # Same symbol split in two, keep the longer part
            if b["duration"] > groups[-1][-1]["duration"]:
                groups[-1][-1] = b
        elif groups and b["pos"] - groups[-1][-1]["pos"] <= 2:
            groups[-1].append(b)
        else:
            groups.append([b])
    if not groups:
        return []
    best = max(groups, key=len)
    first = best[0]["pos"]
    for b in best:
        b["pos"] -= first
    return best


def fit_template(blinks):
    """Find the template offset that best explains the observed colors.

    "warm" blinks may be red (HW fault) or amber (separator); which one is
    decided by where they land in the template. Returns all offsets with
    the minimal number of mismatches.
    """
    last_pos = blinks[-1]["pos"]
    best = []
    best_cost = None
    for offset in range(-last_pos, NUM_SLOTS):
        cost = 0
        for b in blinks:
            slot = b["pos"] + offset
            if not 0 <= slot < NUM_SLOTS:
                cost += 1
                continue
            allowed = slot_allowed(slot)
            if b["color"] == "warm":
                ok = bool(allowed & {"red", "amber"})
            else:
                ok = b["color"] in allowed
            cost += 0 if ok else 1
        if best_cost is None or cost < best_cost:
            best, best_cost = [offset], cost
        elif cost == best_cost:
            best.append(offset)
    return best, best_cost


def decode(blinks, offset, platform):
    by_slot = {b["pos"] + offset: b for b in blinks}
    names = dict(HW_BITS_COMMON)
    names.update(HW_BITS_PLATFORM[platform])

    hw = {}
    for slot in range(SLOT_HW_FIRST, SLOT_HW_LAST + 1):
        b = by_slot.get(slot)
        if b is None:
            hw[slot] = None
        else:
            hw[slot] = b["color"] in ("warm", "red")

    bios_value = 0
    bios_known = 0
    for bit in range(16):
        b = by_slot.get(SLOT_BIOS_FIRST + bit)
        if b is None:
            continue
        bios_known |= 1 << bit
        if b["color"] == "blue":
            bios_value |= 1 << bit

    return names, hw, bios_value, bios_known, by_slot


def slot_name(slot):
    if slot == SLOT_WHITE:
        return "start"
    if slot <= SLOT_HW_LAST:
        return f"HW bit {slot}"
    if slot == SLOT_AMBER:
        return "separator"
    if slot <= SLOT_BIOS_LAST:
        return f"BIOS bit {slot - SLOT_BIOS_FIRST}"
    return "-"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("video")
    parser.add_argument(
        "--platform",
        choices=sorted(HW_BITS_PLATFORM),
        default="fw13",
        help="selects names for HW bits 6-9 (default: %(default)s)",
    )
    parser.add_argument(
        "--led",
        metavar="X,Y",
        help="initial LED position, skips the initial full frame search",
    )
    parser.add_argument(
        "--roi",
        type=int,
        help="tracking window half size in px (default: 15%% of height)",
    )
    parser.add_argument(
        "--min-pixels",
        type=int,
        default=6,
        help="lit pixels needed to count the LED as on (default: %(default)s)",
    )
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    width, height, fps = probe_video(args.video)
    center = None
    if args.led:
        center = tuple(int(v) for v in args.led.split(","))
    roi = args.roi or int(height * 0.15)
    print(f"Video {width}x{height} @ {fps:.2f} fps")

    frames = analyze_frames(
        args.video, width, height, fps, center, roi, args.min_pixels
    )
    blinks = segment_blinks(frames, fps)
    if not blinks:
        sys.exit("no LED blinks found, try --led X,Y")

    # Anything lit much longer than one symbol is regular LED behavior
    # (charging, power on, ...) and not part of the diagnostic code.
    steady = [b for b in blinks if b["duration"] > DIAG_PERIOD_S * 1.25]
    blinks = [b for b in blinks if b not in steady]
    if steady and args.verbose:
        for b in steady:
            print(
                f"ignoring steady {b['color']} at {b['start']:.2f}s "
                f"for {b['duration']:.2f}s"
            )
    if not blinks:
        sys.exit("only steady LED states found")

    period = estimate_period(blinks)
    blinks = assign_slots(blinks, period)
    if not blinks:
        sys.exit("no blinks on a diagnostic time grid found")

    offsets, cost = fit_template(blinks)
    offset = offsets[0]
    if len(offsets) > 1:
        print(
            f"warning: {len(offsets)} equally good alignments, using first",
            file=sys.stderr,
        )
    if cost:
        print(
            f"warning: {cost} blink(s) don't fit the template", file=sys.stderr
        )

    names, hw, bios_value, bios_known, by_slot = decode(
        blinks, offset, args.platform
    )

    print(f"\n{len(blinks)} blinks, period {period * 1000:.0f} ms\n")
    print("  time   slot           color   hue")
    for b in blinks:
        slot = b["pos"] + offset
        color = b["color"]
        if color == "warm":
            color = "amber" if slot == SLOT_AMBER else "red"
        hue = f"{b['hue']:.0f}" if b["hue"] is not None else "-"
        mark = "" if color in slot_allowed(slot) else "  <- unexpected"
        extra = f"  {b['votes']}" if args.verbose else ""
        print(
            f"  {b['start']:5.2f}s {slot_name(slot):14s} {color:7s} {hue:>4s}"
            f"{mark}{extra}"
        )

    missing = [slot_name(s) for s in range(NUM_SLOTS) if s not in by_slot]
    if missing:
        print(f"\nNot in video: {', '.join(missing)}")

    print(f"\nHardware diagnostics ({args.platform}):")
    for bit, fault in hw.items():
        state = "?" if fault is None else ("FAULT" if fault else "ok")
        print(f"  {bit:2d} {names[bit]:38s} {state}")

    print("\nLast port80 code: ", end="")
    if bios_known == 0xFFFF:
        print(f"0x{bios_value:04x}")
    else:
        digits = "".join(
            str((bios_value >> b) & 1) if bios_known & (1 << b) else "?"
            for b in reversed(range(16))
        )
        print(f"0b{digits} (incomplete)")


if __name__ == "__main__":
    main()
