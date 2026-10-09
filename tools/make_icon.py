import pathlib, subprocess, tempfile
from PIL import Image

root = pathlib.Path(__file__).resolve().parent.parent
svg = (root.parent / "Framey" / "icon.svg").read_text()
chrome = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
tmp = pathlib.Path(tempfile.mkdtemp())
html = tmp / "icon.html"
html.write_text('<!doctype html><html><body style="margin:0;background:transparent"><div style="width:256px;height:256px">' + svg.replace("<svg ", '<svg width="256" height="256" ', 1) + "</div></body></html>")
png = tmp / "icon.png"
subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--default-background-color=00000000", "--window-size=256,256", "--screenshot=" + str(png), html.as_uri()], check=True, capture_output=True)
img = Image.open(png).convert("RGBA")
(root / "assets").mkdir(exist_ok=True)
img.save(root / "assets" / "framey.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icon", img.size, (root / "assets" / "framey.ico").stat().st_size, "bytes; corner pixel", img.getpixel((2, 2)))
