#!/usr/bin/env python3
"""
Convert SEAN_FINAL_PROJECT_REPORT.md to a publication-grade academic PDF.
Utilizes Python (mistune, base64, re) + KaTeX + Chrome Headless.
"""

import os
import re
import sys
import base64
import subprocess
from pathlib import Path

WORKSPACE = Path("/Users/ravo/Downloads/ns-3.49")
REPORT_MD = WORKSPACE / "reports" / "SEAN_FINAL_PROJECT_REPORT.md"
OUTPUT_HTML = WORKSPACE / "reports" / "SEAN_FINAL_PROJECT_REPORT_print.html"
OUTPUT_PDF = WORKSPACE / "reports" / "SEAN_FINAL_PROJECT_REPORT.pdf"

CHROME_PATH = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

def encode_image(img_rel_path):
    """Read local image file and return base64 data URI."""
    full_path = WORKSPACE / img_rel_path
    if not full_path.exists():
        # Try relative to reports
        full_path = WORKSPACE / "reports" / img_rel_path
    if not full_path.exists():
        print(f"Warning: image {img_rel_path} not found at {full_path}")
        return ""
    with open(full_path, "rb") as f:
        data = f.read()
    b64 = base64.b64encode(data).decode('utf-8')
    return f"data:image/png;base64,{b64}"

def main():
    if not REPORT_MD.exists():
        print(f"Error: {REPORT_MD} does not exist.")
        sys.exit(1)

    with open(REPORT_MD, "r", encoding="utf-8") as f:
        md_text = f.read()

    # Process GitHub alerts: > [!NOTE], > [!IMPORTANT], etc.
    alert_pattern = re.compile(
        r'(>\s+\[!(IMPORTANT|NOTE|TIP|WARNING|CAUTION)\]\s*\n(?:>.*\n?)+)'
    )
    def replace_alert(match):
        full_match = match.group(1)
        alert_type = match.group(2)
        lines = [re.sub(r'^>\s?', '', l) for l in full_match.split('\n')]
        # Drop the [!ALERT] line
        body_lines = [l for l in lines if not re.match(r'\[!(IMPORTANT|NOTE|TIP|WARNING|CAUTION)\]', l)]
        body_text = "\n".join(body_lines).strip()
        return f'<div class="callout callout-{alert_type.lower()}"><div class="callout-header">{alert_type}</div><div class="callout-body">{body_text}</div></div>\n'

    processed_md = alert_pattern.sub(replace_alert, md_text)

    # Convert to HTML via mistune with custom renderer allowing file:// URLs
    import mistune
    class AcademicRenderer(mistune.HTMLRenderer):
        def safe_url(self, url):
            # Allow file:// and local paths in addition to http/https
            return url

    markdown = mistune.create_markdown(renderer=AcademicRenderer(), plugins=['table'])
    html_body = markdown(processed_md)

    # Post-process: Embed base64 images directly into HTML for key figures
    def replace_figure_html(match):
        caption = match.group(1)
        rel_path = match.group(2)
        desc = match.group(3)
        data_uri = encode_image(rel_path)
        img_html = ""
        if data_uri:
            img_html = f'''
<div class="figure-block">
  <img src="{data_uri}" alt="{caption}" class="report-figure" />
  <div class="figure-caption"><strong>Figure:</strong> {caption} — {desc}</div>
</div>'''
        return f'<li><strong>{caption}:</strong> <a href="file://{WORKSPACE / rel_path}"><code>{rel_path}</code></a> {desc}{img_html}</li>'

    html_body = re.sub(
        r'<li><strong>([^*<]+):</strong>\s*<a[^>]*><code>(reports/figures/[^<]+\.png)</code></a>\s*([^<]+)</li>',
        replace_figure_html,
        html_body
    )

    # Style Section 1 as Title Page
    # Wrap Title Page in title-page div
    title_split = re.split(r'(<h2[^>]*>2\.\s+ABSTRACT</h2>)', html_body, maxsplit=1)
    if len(title_split) == 3:
        html_body = f'<div class="title-page">{title_split[0]}</div>\n{title_split[1]}{title_split[2]}'

    full_html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Smart Emergency Alert and Priority Messaging Network (SEAN) — Academic Project Report</title>

<!-- KaTeX support -->
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.css">
<script src="https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/contrib/auto-render.min.js"></script>

<style>
@page {{
  size: A4 portrait;
  margin: 20mm 18mm 20mm 18mm;
  @top-right {{
    content: "SEAN Project Report | ns-3.49";
    font-family: 'Helvetica Neue', Arial, sans-serif;
    font-size: 8pt;
    color: #666;
  }}
  @bottom-center {{
    content: counter(page);
    font-family: 'Helvetica Neue', Arial, sans-serif;
    font-size: 9pt;
    color: #444;
  }}
}}

*, *:before, *:after {{
  box-sizing: border-box;
}}

body {{
  font-family: 'Times New Roman', 'Liberation Serif', Times, serif;
  font-size: 10.5pt;
  line-height: 1.48;
  color: #111;
  background-color: #fff;
  margin: 0;
  padding: 0;
}}

/* Title Page Styling */
.title-page {{
  page-break-after: always;
  padding-top: 40px;
  text-align: center;
}}

.title-page h1 {{
  font-size: 22pt;
  font-weight: bold;
  letter-spacing: 0.5px;
  line-height: 1.25;
  color: #0d233a;
  margin-bottom: 8px;
}}

.title-page h2 {{
  font-size: 13pt;
  font-weight: normal;
  color: #444;
  margin-top: 0;
  margin-bottom: 24px;
}}

.title-page h3 {{
  font-size: 14pt;
  text-transform: uppercase;
  letter-spacing: 1.5px;
  color: #1a4a75;
  margin-top: 30px;
  margin-bottom: 15px;
  border-bottom: 2px solid #1a4a75;
  display: inline-block;
  padding-bottom: 4px;
}}

.title-page p {{
  font-size: 10.5pt;
  line-height: 1.5;
  margin: 6px 0;
}}

.title-page h4 {{
  font-size: 11pt;
  text-transform: uppercase;
  letter-spacing: 1px;
  margin-top: 25px;
  margin-bottom: 10px;
  color: #333;
}}

.title-page ul {{
  list-style: none;
  padding-left: 0;
  margin: 10px auto;
  max-width: 550px;
  text-align: left;
  border: 1px solid #ccc;
  background-color: #fcfcfc;
  padding: 15px 25px;
  border-radius: 4px;
}}

.title-page ul li {{
  margin: 6px 0;
  font-size: 10pt;
}}

/* Headings */
h1, h2, h3, h4, h5, h6 {{
  font-family: 'Helvetica Neue', Arial, sans-serif;
  color: #1a252f;
  margin-top: 1.5em;
  margin-bottom: 0.5em;
  page-break-after: avoid;
  break-after: avoid;
}}

h1 {{
  font-size: 18pt;
  border-bottom: 2px solid #2c3e50;
  padding-bottom: 4px;
}}

h2 {{
  font-size: 13.5pt;
  border-bottom: 1px solid #bdc3c7;
  padding-bottom: 3px;
  margin-top: 1.8em;
}}

h3 {{
  font-size: 11.5pt;
  color: #2c3e50;
  margin-top: 1.3em;
}}

h4 {{
  font-size: 10.5pt;
  color: #34495e;
}}

p {{
  margin-top: 0;
  margin-bottom: 0.85em;
  text-align: justify;
  text-justify: inter-word;
}}

/* Lists */
ul, ol {{
  margin-top: 0;
  margin-bottom: 0.85em;
  padding-left: 24px;
}}

li {{
  margin-bottom: 0.35em;
}}

/* Tables */
table {{
  width: 100%;
  border-collapse: collapse;
  margin: 1.2em 0;
  font-size: 9pt;
  page-break-inside: avoid;
  break-inside: avoid;
}}

th, td {{
  border: 1px solid #d0d7de;
  padding: 5px 7px;
  text-align: left;
  vertical-align: top;
}}

th {{
  background-color: #f1f4f8;
  font-family: 'Helvetica Neue', Arial, sans-serif;
  font-weight: 600;
  color: #24292f;
  border-bottom: 2px solid #a0aab5;
}}

tr:nth-child(even) {{
  background-color: #fbfcfd;
}}

/* Code Blocks & ASCII Diagrams */
pre {{
  background-color: #f6f8fa;
  border: 1px solid #d0d7de;
  border-radius: 4px;
  padding: 10px 12px;
  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  font-size: 8pt;
  line-height: 1.35;
  overflow-x: auto;
  page-break-inside: avoid;
  break-inside: avoid;
  margin: 1em 0;
}}

code {{
  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  font-size: 9pt;
  background-color: #f0f3f6;
  padding: 1px 4px;
  border-radius: 3px;
  border: 1px solid #e1e4e8;
}}

pre code {{
  background-color: transparent;
  padding: 0;
  border: none;
}}

/* Callouts / Alerts */
.callout {{
  margin: 1.2em 0;
  padding: 10px 14px;
  border-left: 4px solid #0969da;
  background-color: #f6f8fa;
  border-radius: 0 4px 4px 0;
  page-break-inside: avoid;
  break-inside: avoid;
}}

.callout-header {{
  font-family: 'Helvetica Neue', Arial, sans-serif;
  font-weight: bold;
  font-size: 9.5pt;
  margin-bottom: 4px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}}

.callout-important {{
  border-left-color: #8250df;
  background-color: #fbf7ff;
}}
.callout-important .callout-header {{ color: #8250df; }}

.callout-note {{
  border-left-color: #0969da;
  background-color: #f3f8fc;
}}
.callout-note .callout-header {{ color: #0969da; }}

.callout-tip {{
  border-left-color: #1a7f37;
  background-color: #f3faf5;
}}
.callout-tip .callout-header {{ color: #1a7f37; }}

.callout-warning {{
  border-left-color: #9a6700;
  background-color: #fffbeb;
}}
.callout-warning .callout-header {{ color: #9a6700; }}

.callout-body p:last-child {{
  margin-bottom: 0;
}}

/* Figures */
.figure-block {{
  margin: 1.4em auto;
  text-align: center;
  page-break-inside: avoid;
  break-inside: avoid;
}}

.report-figure {{
  max-width: 88%;
  height: auto;
  border: 1px solid #e1e4e8;
  border-radius: 4px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.06);
}}

.figure-caption {{
  font-family: 'Helvetica Neue', Arial, sans-serif;
  font-size: 8.5pt;
  color: #444;
  margin-top: 6px;
  font-style: italic;
}}

/* Blockquotes */
blockquote {{
  margin: 1em 0;
  padding: 6px 14px;
  border-left: 3px solid #d0d7de;
  color: #57606a;
  background-color: #fafbfc;
}}

/* Math displays */
.katex-display {{
  margin: 0.8em 0 !important;
  page-break-inside: avoid;
}}

/* Links */
a {{
  color: #0969da;
  text-decoration: none;
}}
</style>
</head>
<body>

{html_body}

<script>
document.addEventListener('DOMContentLoaded', function() {{
  renderMathInElement(document.body, {{
    delimiters: [
      {{left: '$$', right: '$$', display: true}},
      {{left: '$', right: '$', display: false}}
    ],
    throwOnError: false
  }});
}});
</script>

</body>
</html>'''

    with open(OUTPUT_HTML, "w", encoding="utf-8") as f:
        f.write(full_html)

    print(f"HTML generation complete: {OUTPUT_HTML} ({len(full_html)} bytes)")

    # Execute Chrome headless conversion
    chrome_cmd = [
        CHROME_PATH,
        "--headless=new",
        "--disable-gpu",
        "--run-all-compositor-stages-before-draw",
        "--virtual-time-budget=8000",
        "--no-pdf-header-footer",
        f"--print-to-pdf={OUTPUT_PDF}",
        str(OUTPUT_HTML)
    ]

    print("Running Chrome headless conversion...")
    res = subprocess.run(chrome_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Chrome error: {res.stderr}")
    
    if OUTPUT_PDF.exists():
        pdf_size = OUTPUT_PDF.stat().st_size
        print(f"SUCCESS: PDF created at {OUTPUT_PDF} ({pdf_size:,} bytes)")
    else:
        print("ERROR: PDF was not created.")
        sys.exit(1)

if __name__ == "__main__":
    main()
