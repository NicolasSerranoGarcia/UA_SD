# WaterManagement report

[WaterManagement-report.md](WaterManagement-report.md) is the editable source.
The matching [PDF](WaterManagement-report.pdf) is ready to review and the
[Word document](WaterManagement-report.docx) can be edited for submission.
Five screenshot placeholders name the real captures to insert before handing
in the report. Replace them in the Markdown source before exporting again.

The assignment allows a free document format (pp. 13–14). The report includes
the cover details, component/source explanation, deployment procedure and
verification results. Submit it alongside the source and compiled binaries;
the correction guide is a separate document to print double-sided and complete
in the student column. Prepare the cloud services and images before the lab
demonstration, and check instructor repository access and prompt records.

## Export

Run from the repository root with Pandoc installed. PDF export also needs
XeLaTeX. The commands below work as single lines in PowerShell or Bash.

```sh
pandoc docs/report/WaterManagement-report.md --lua-filter=docs/report/export.lua --resource-path=docs/report --reference-doc=docs/report/reference.docx -o docs/report/WaterManagement-report.docx
pandoc docs/report/WaterManagement-report.md --lua-filter=docs/report/export.lua --resource-path=docs/report --pdf-engine=xelatex -V papersize=a4 -V fontsize=11pt -V geometry:margin=22mm -V colorlinks=true -o docs/report/WaterManagement-report.pdf
```

`reference.docx` supplies the Word page layout and styles. `export.lua` preserves
page breaks and keeps short PDF examples and screenshot prompts together.
The architecture diagram's editable source is `figures/architecture.dot`;
Graphviz regenerates its PNG/SVG assets:

```sh
dot -Tpng -Gdpi=160 docs/report/figures/architecture.dot -o docs/report/figures/architecture.png
dot -Tsvg docs/report/figures/architecture.dot -o docs/report/figures/architecture.svg
```

After inserting screenshots, check pagination and update any evidence that
depends on the final connected run. Local WS fixtures and mock-broker results
are identified separately from that full-system demonstration in the report.
