
import re

raw = "\nIP                 PORT     LOAD (%)     ACTIVE      \n────────────────   ──────   ───────────  ─────────── \n172.30.2.21        7777     24.2%        0           \n"

# The regex from app.js: (\d+\.\d+\.\d+\.\d+)[\s:]+(\d+)\s+([\d.]+)\s*%?\s+(\d+)?
pattern = r"(\d+\.\d+\.\d+\.\d+)[\s:]+(\d+)\s+([\d.]+)\s*%?\s+(\d+)?"

matches = list(re.finditer(pattern, raw))
print(f"Testing regex... Found {len(matches)} matches.")

for m in matches:
    print(f"Match found!")
    print(f"IP: {m.group(1)}")
    print(f"Port: {m.group(2)}")
    print(f"Load: {m.group(3)}")
    print(f"Tasks: {m.group(4)}")
