#!/usr/bin/env python3
"""Render an asciinema .cast recording to an animated GIF (and PNG frames).

Replays the terminal stream through pyte and draws frames with PIL.
"""
import json
import sys

import pyte
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
CELL_W, CELL_H = 9, 18
FPS = 10
BG, FG = (30, 30, 30), (220, 220, 220)


def load_events(path):
    events = []
    with open(path) as f:
        header = json.loads(f.readline())
        for line in f:
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return header, events


def render_frame(screen, font):
    w, h = screen.columns * CELL_W, screen.lines * CELL_H
    img = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(img)
    for y, line in enumerate(screen.display):
        draw.text((2, y * CELL_H), line, font=font, fill=FG)
    return img


def main():
    cast_path, out_gif = sys.argv[1], sys.argv[2]
    frames_dir = sys.argv[3] if len(sys.argv) > 3 else None
    header, events = load_events(cast_path)
    cols, rows = header["width"], header["height"]
    screen = pyte.Screen(cols, rows)
    stream = pyte.ByteStream(screen)

    duration = events[-1][0] if events else 0
    frames = []
    ei = 0
    t = 0.0
    while t <= duration:
        while ei < len(events) and events[ei][0] <= t:
            ev = events[ei]
            if ev[1] == "o":
                stream.feed(ev[2].encode("utf-8"))
            ei += 1
        frames.append(render_frame(screen, font))
        t += 1.0 / FPS
    frames[0].save(
        out_gif, save_all=True, append_images=frames[1:],
        duration=int(1000 / FPS), loop=0, optimize=False,
    )
    if frames_dir:
        import os
        os.makedirs(frames_dir, exist_ok=True)
        for i, frame in enumerate(frames):
            frame.save(os.path.join(frames_dir, f"frame_{i:05d}.png"))
    print(f"wrote {out_gif}: {len(frames)} frames, {duration:.1f}s")


if __name__ == "__main__":
    font = ImageFont.truetype(FONT_PATH, 14)
    main()
