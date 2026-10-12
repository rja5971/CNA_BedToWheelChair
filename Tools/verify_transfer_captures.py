"""Verify the rendered training smoke captures, independently of MID values."""
import json
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / "Saved/Guidance"
black = Image.open(root / "FadeBlack.png").convert("RGB")
maximum = max(high for low, high in black.getextrema())
assert maximum <= 1, f"Fade did not cover the full rendered viewport: maximum RGB {maximum}"
grab = Image.open(root / "GrabHint.png").convert("RGB")
cyan = sum(1 for r, g, b in grab.get_flattened_data() if r < 80 and g > 160 and b > 190)
assert cyan > 70, f"Grab capture lacks visible cyan guidance: {cyan} pixels"
result = {"fade_dimensions": black.size, "maximum_black_rgb": maximum, "grab_cyan_pixels": cyan}
(root / "CaptureVerification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result, indent=2))
