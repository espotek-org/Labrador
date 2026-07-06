#!/usr/bin/env python3
"""Generate a browsable prediction-QA report from captured before/after frames.

Pipeline (see QaSuite.cpp RegisterPredictScenarios):
  1. `labrador --qa=predict` runs each scenario, capturing
     <id>_before.ppm / <id>_after.ppm into LABRADOR_QA_CAPTURE_DIR.
  2. A model inspects each before frame, predicts the interaction's result,
     inspects the after frame, and records prediction/actual/verdict in a
     findings JSON (schema: see qa_predict_findings.json).
  3. This script embeds the frames (as PNG data URIs) alongside the findings
     into one self-contained HTML page. A human ticks Bug / Not a bug / Unsure
     per scenario, adds notes, and exports the reconciled JSON.

Usage:
  qa_predict_report.py --captures <dir> --findings <json> --out <html>
                       [--max-width 1200]

Only depends on the stdlib + `sips` (macOS) or ImageMagick `convert` for
PPM->PNG; falls back to embedding PPM-derived PNG via `sips` by default.
"""
import argparse, base64, json, subprocess, sys, os, tempfile, html


def ppm_to_png_bytes(ppm_path, max_width):
    """Convert a PPM to downscaled PNG bytes using sips (macOS) or convert."""
    with tempfile.TemporaryDirectory() as td:
        png = os.path.join(td, "f.png")
        if _has("sips"):
            subprocess.run(["sips", "-s", "format", "png", ppm_path, "--out", png],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["sips", "--resampleWidth", str(max_width), png, "--out", png],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif _has("convert"):
            subprocess.run(["convert", ppm_path, "-resize", f"{max_width}x", png], check=True)
        else:
            sys.exit("need `sips` or `convert` to rasterise PPM frames")
        with open(png, "rb") as f:
            return f.read()


def _has(cmd):
    from shutil import which
    return which(cmd) is not None


def data_uri(png_bytes):
    return "data:image/png;base64," + base64.b64encode(png_bytes).decode("ascii")


def frame_uri(captures, sid, which, max_width):
    for ext in (".ppm", ".png"):
        p = os.path.join(captures, f"{sid}_{which}{ext}")
        if os.path.exists(p):
            if ext == ".ppm":
                return data_uri(ppm_to_png_bytes(p, max_width))
            with open(p, "rb") as f:
                return data_uri(f.read())
    return ""


def esc(s):
    return html.escape(s or "", quote=True)


def build(findings, captures, max_width, show_matched=False):
    """A real run has thousands of scenarios and almost all of them match the
    model's prediction — the human must only ever see the divergences. Matched
    scenarios are counted in the summary but get no card and no embedded
    frames (embedding every frame would also make the page gigabytes).
    `consistent` is a required per-scenario field: it IS the report's gate,
    so a scenario without a verdict on prediction-vs-actual is a broken run.
    """
    scen = findings["scenarios"]
    matched = [s for s in scen if s["consistent"]]
    shown = scen if show_matched else [s for s in scen if not s["consistent"]]
    for s in shown:
        s["_before"] = frame_uri(captures, s["id"], "before", max_width)
        s["_after"] = frame_uri(captures, s["id"], "after", max_width)
    payload = json.dumps({**findings, "scenarios": shown}, ensure_ascii=False)
    cards = "\n".join(card(i, s) for i, s in enumerate(shown))
    if not cards:
        cards = ('<p class="allclear">All scenarios matched their predictions — '
                 "nothing needs review.</p>")
    return PAGE.replace("{{TITLE}}", esc(findings.get("title", "Prediction QA"))) \
               .replace("{{SUBTITLE}}", esc(findings.get("subtitle", ""))) \
               .replace("{{RUNLABEL}}", esc(findings.get("run_label", ""))) \
               .replace("{{TOTAL}}", str(len(scen))) \
               .replace("{{MATCHED}}", str(len(matched))) \
               .replace("{{SHOWN}}", str(len(shown))) \
               .replace("{{CARDS}}", cards) \
               .replace("{{PAYLOAD}}", base64.b64encode(payload.encode()).decode()), \
           len(matched), len(shown)


def card(i, s):
    mv = s.get("model_verdict", "review")
    return f"""
<section class="card" data-id="{esc(s['id'])}" data-model-verdict="{esc(mv)}">
  <header class="card-head">
    <span class="idx">{i+1:02d}</span>
    <h2>{esc(s['title'])}</h2>
    <span class="mv mv-{esc(mv)}">model: {esc(mv.replace('_',' '))}</span>
  </header>
  <div class="meta">
    <div><span class="lbl">State</span><p>{esc(s.get('state',''))}</p></div>
    <div><span class="lbl">Interaction</span><p class="mono">{esc(s.get('interaction',''))}</p></div>
  </div>
  <div class="frames">
    <figure><figcaption>before</figcaption><img loading="lazy" src="{s['_before']}" alt="before {esc(s['id'])}"></figure>
    <figure><figcaption>after</figcaption><img loading="lazy" src="{s['_after']}" alt="after {esc(s['id'])}"></figure>
  </div>
  <div class="pa">
    <div class="pred"><span class="lbl">Predicted</span><p>{esc(s.get('prediction',''))}</p></div>
    <div class="act"><span class="lbl">Actual</span><p>{esc(s.get('actual',''))}</p></div>
  </div>
  <div class="verdict">
    <span class="lbl">Your call</span>
    <div class="chips" role="radiogroup" aria-label="verdict for {esc(s['id'])}">
      <label class="chip chip-bug"><input type="radio" name="v_{esc(s['id'])}" value="bug"><span>Bug</span></label>
      <label class="chip chip-not"><input type="radio" name="v_{esc(s['id'])}" value="not_bug"><span>Not a bug</span></label>
      <label class="chip chip-unsure"><input type="radio" name="v_{esc(s['id'])}" value="unsure"><span>Unsure</span></label>
    </div>
    <textarea class="notes" data-id="{esc(s['id'])}" placeholder="Notes (repro steps, severity, where to look)…">{esc(s.get('note',''))}</textarea>
  </div>
</section>"""


PAGE = r"""<title>{{TITLE}}</title>
<style>
:root{
  --bg:#0e1012; --panel:#16191b; --panel-2:#1c2023; --line:#2a2f33;
  --ink:#c9cdd2; --ink-dim:#7c828a; --accent:#e0a03a;
  --bug:#e5484d; --not:#5fcf80; --unsure:#8b9096;
  --radius:12px; --mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  --sans:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
}
:root[data-theme="light"]{
  --bg:#f2f0ec; --panel:#ffffff; --panel-2:#f7f5f1; --line:#e0ddd6;
  --ink:#23272b; --ink-dim:#6a7076; --accent:#b9781a;
}
@media (prefers-color-scheme:light){
  :root:not([data-theme="dark"]){
    --bg:#f2f0ec; --panel:#ffffff; --panel-2:#f7f5f1; --line:#e0ddd6;
    --ink:#23272b; --ink-dim:#6a7076; --accent:#b9781a;
  }
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);
  line-height:1.5;-webkit-font-smoothing:antialiased}
.wrap{max-width:1200px;margin:0 auto;padding:32px 24px 96px}
header.top{border-bottom:1px solid var(--line);padding-bottom:20px;margin-bottom:28px}
.eyebrow{font-family:var(--mono);font-size:12px;letter-spacing:.14em;text-transform:uppercase;
  color:var(--accent);display:flex;align-items:center;gap:8px}
.eyebrow::before{content:"";width:8px;height:8px;border-radius:50%;background:var(--accent);
  box-shadow:0 0 10px var(--accent)}
h1{font-size:30px;line-height:1.15;margin:10px 0 8px;text-wrap:balance;font-weight:650}
.sub{color:var(--ink-dim);max-width:66ch;margin:0}
.summary{display:flex;gap:12px;flex-wrap:wrap;margin-top:18px}
.allclear{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);
  padding:28px 24px;text-align:center;color:var(--ink-dim);font-size:15px}
.stat{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px 16px;
  font-family:var(--mono);font-size:13px}
.stat b{font-size:20px;display:block;font-variant-numeric:tabular-nums}
.stat.flag b{color:var(--bug)} .stat.ok b{color:var(--not)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);
  padding:20px;margin-bottom:22px}
.card-head{display:flex;align-items:center;gap:12px;margin-bottom:14px}
.idx{font-family:var(--mono);color:var(--ink-dim);font-size:13px}
.card-head h2{font-size:18px;margin:0;flex:1;font-weight:600}
.mv{font-family:var(--mono);font-size:11px;letter-spacing:.06em;text-transform:uppercase;
  padding:3px 9px;border-radius:20px;border:1px solid var(--line);color:var(--ink-dim)}
.mv-not_bug{color:var(--not);border-color:color-mix(in srgb,var(--not) 45%,transparent)}
.mv-bug{color:var(--bug);border-color:color-mix(in srgb,var(--bug) 45%,transparent)}
.lbl{font-family:var(--mono);font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--ink-dim)}
.meta{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:16px}
.meta p{margin:4px 0 0} .mono{font-family:var(--mono);font-size:13px}
.frames{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:16px}
.frames figure{margin:0}
.frames figcaption{font-family:var(--mono);font-size:11px;letter-spacing:.1em;text-transform:uppercase;
  color:var(--ink-dim);margin-bottom:6px}
.frames img{width:100%;height:auto;border-radius:8px;border:1px solid var(--line);display:block;background:#000}
.pa{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:16px;
  border-top:1px solid var(--line);padding-top:14px}
.pa p{margin:5px 0 0} .act{color:var(--ink)}
.pred{border-left:2px solid color-mix(in srgb,var(--accent) 55%,transparent);padding-left:12px}
.act{border-left:2px solid color-mix(in srgb,var(--not) 45%,transparent);padding-left:12px}
.verdict{border-top:1px solid var(--line);padding-top:14px;display:flex;flex-direction:column;gap:10px}
.chips{display:flex;gap:8px;flex-wrap:wrap}
.chip{position:relative;display:inline-flex;align-items:center;gap:7px;cursor:pointer;
  border:1px solid var(--line);border-radius:8px;padding:7px 14px;font-size:14px;
  background:var(--panel-2);user-select:none;transition:border-color .12s,background .12s}
.chip input{position:absolute;opacity:0;pointer-events:none}
.chip::before{content:"";width:11px;height:11px;border-radius:50%;border:1.5px solid var(--ink-dim)}
.chip-bug:has(:checked){border-color:var(--bug);background:color-mix(in srgb,var(--bug) 14%,var(--panel-2))}
.chip-bug:has(:checked)::before{background:var(--bug);border-color:var(--bug)}
.chip-not:has(:checked){border-color:var(--not);background:color-mix(in srgb,var(--not) 14%,var(--panel-2))}
.chip-not:has(:checked)::before{background:var(--not);border-color:var(--not)}
.chip-unsure:has(:checked){border-color:var(--unsure);background:color-mix(in srgb,var(--unsure) 16%,var(--panel-2))}
.chip-unsure:has(:checked)::before{background:var(--unsure);border-color:var(--unsure)}
.chip:focus-within{outline:2px solid var(--accent);outline-offset:2px}
.notes{width:100%;min-height:52px;resize:vertical;background:var(--panel-2);color:var(--ink);
  border:1px solid var(--line);border-radius:8px;padding:9px 11px;font-family:var(--sans);font-size:13px}
.notes:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
.actionbar{position:sticky;bottom:0;margin-top:8px;display:flex;gap:12px;align-items:center;
  background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(8px);
  border-top:1px solid var(--line);padding:14px 24px;justify-content:flex-end}
.actionbar .count{margin-right:auto;font-family:var(--mono);font-size:13px;color:var(--ink-dim)}
button.export{font:inherit;font-weight:600;background:var(--accent);color:#1a1205;border:0;
  border-radius:9px;padding:10px 20px;cursor:pointer}
button.export:hover{filter:brightness(1.06)} button.export:focus-visible{outline:2px solid var(--ink);outline-offset:2px}
@media (max-width:720px){.meta,.pa,.frames{grid-template-columns:1fr}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>

<div class="wrap">
  <header class="top">
    <div class="eyebrow">Prediction QA · {{RUNLABEL}}</div>
    <h1>{{TITLE}}</h1>
    <p class="sub">{{SUBTITLE}}</p>
    <div class="summary">
      <div class="stat"><b>{{TOTAL}}</b>scenarios ran</div>
      <div class="stat ok"><b>{{MATCHED}}</b>matched prediction (hidden)</div>
      <div class="stat flag"><b>{{SHOWN}}</b>divergences to review</div>
      <div class="stat flag"><b id="s-bug">0</b>marked bug</div>
      <div class="stat ok"><b id="s-not">0</b>marked not-a-bug</div>
      <div class="stat"><b id="s-todo">{{SHOWN}}</b>unreviewed</div>
    </div>
  </header>
  <main>{{CARDS}}</main>
</div>
<div class="actionbar">
  <span class="count" id="count"></span>
  <button class="export" id="export">Export JSON</button>
</div>

<script>
const payload = JSON.parse(atob("{{PAYLOAD}}"));
function tally(){
  let bug=0,not=0,todo=0;
  for(const s of payload.scenarios){
    const c=document.querySelector('input[name="v_'+CSS.escape(s.id)+'"]:checked');
    if(!c) todo++; else if(c.value==='bug') bug++; else if(c.value==='not_bug') not++;
  }
  document.getElementById('s-bug').textContent=bug;
  document.getElementById('s-not').textContent=not;
  document.getElementById('s-todo').textContent=todo;
  document.getElementById('count').textContent=(payload.scenarios.length-todo)+' / '+payload.scenarios.length+' reviewed';
}
document.addEventListener('change',tally);
tally();
document.getElementById('export').addEventListener('click',()=>{
  const out={title:payload.title,run_label:payload.run_label,scenarios:[]};
  for(const s of payload.scenarios){
    const c=document.querySelector('input[name="v_'+CSS.escape(s.id)+'"]:checked');
    const notes=document.querySelector('textarea[data-id="'+CSS.escape(s.id)+'"]');
    out.scenarios.push({id:s.id,title:s.title,state:s.state,interaction:s.interaction,
      prediction:s.prediction,actual:s.actual,model_verdict:s.model_verdict,
      consistent:s.consistent,human_verdict:c?c.value:null,notes:notes?notes.value:""});
  }
  const blob=new Blob([JSON.stringify(out,null,2)],{type:"application/json"});
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);
  a.download="qa_predict_reviewed.json";a.click();URL.revokeObjectURL(a.href);
});
</script>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--captures", required=True, help="dir with <id>_before/after.ppm|.png")
    ap.add_argument("--findings", required=True, help="findings JSON")
    ap.add_argument("--out", required=True, help="output HTML")
    ap.add_argument("--max-width", type=int, default=1200)
    ap.add_argument("--show-matched", action="store_true",
                    help="include matched scenarios too (debugging small runs)")
    a = ap.parse_args()
    with open(a.findings) as f:
        findings = json.load(f)
    html_out, matched, shown = build(findings, a.captures, a.max_width, a.show_matched)
    with open(a.out, "w") as f:
        f.write(html_out)
    print(f"wrote {a.out} ({len(html_out)//1024} KB): "
          f"{len(findings['scenarios'])} scenarios, {matched} matched prediction "
          f"(hidden), {shown} shown for review")


if __name__ == "__main__":
    main()
