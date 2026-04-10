
const raw = "\nIP                 PORT     LOAD (%)     ACTIVE      \n────────────────   ──────   ───────────  ─────────── \n172.30.2.21        7777     24.2%        0           \n";

const re = /(\d+\.\d+\.\d+\.\d+)[\s:]+(\d+)\s+([\d.]+)\s*%?\s+(\d+)?/g;
let m;
console.log("Testing regex...");
while ((m = re.exec(raw)) !== null) {
  console.log("Match found!");
  console.log("IP:", m[1]);
  console.log("Port:", m[2]);
  console.log("Load:", m[3]);
  console.log("Tasks:", m[4]);
}
if (!raw.match(re)) {
  console.log("No matches found.");
}
