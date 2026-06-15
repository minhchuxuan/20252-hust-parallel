# report/figures/

This directory holds plots and screenshots for `main.tex`.

Populate it automatically via:

```sh
cd ..            # project root
make plots       # writes speedup.png / efficiency.png / weak_efficiency.png to bench/
make viz         # writes data/sim.gif + data/sim.png
cd report
make figures     # copies the PNGs here
make             # builds main.pdf
```

If a figure is missing when you run `pdflatex`, the build will complain
but still produce a PDF with placeholders for the missing images.
