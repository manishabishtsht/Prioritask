# Draws the PrioriTask icon: three bars getting shorter, a day shrunk to fit.
from PIL import Image, ImageDraw

LANCZOS = getattr(Image, "Resampling", Image).LANCZOS
S = 1024                                   # draw large, then shrink for smooth edges

# Rounded navy tile with a soft top-to-bottom gradient, matching the app.
grad = Image.new("RGBA", (S, S))
gd = ImageDraw.Draw(grad)
top, bottom = (40, 50, 76), (14, 17, 27)
for y in range(S):
    t = y / (S - 1)
    gd.line([(0, y), (S, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=230, fill=255)
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)
d.rounded_rectangle([40, 40, S - 40, S - 40], radius=230, outline=(78, 98, 140, 255), width=14)

# Three bars, each shorter than the last: blue, amber, green.
left, right = 210, S - 210
h, gap = 128, 76
y0 = (S - (3 * h + 2 * gap)) // 2
bars = [(1.00, (70, 136, 246)), (0.70, (236, 172, 64)), (0.42, (76, 196, 124))]
for i, (frac, col) in enumerate(bars):
    y  = y0 + i * (h + gap)
    x1 = left + int((right - left) * frac)
    d.rounded_rectangle([left, y, x1, y + h], radius=h // 2, fill=col + (255,))

# A small tick beside the shortest bar: it still fits.
y  = y0 + 2 * (h + gap)
cx = left + int((right - left) * 0.42) + 150
cy = y + h // 2
d.line([(cx - 62, cy + 2), (cx - 14, cy + 50), (cx + 76, cy - 52)], fill=(235, 240, 250, 255), width=46, joint="curve")

icon = img.resize((256, 256), LANCZOS)
icon.save("prioritask.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
icon.save("prioritask.png")
print("Saved prioritask.ico")
