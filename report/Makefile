# report/Makefile — compile main.tex to main.pdf.
#
#   make          -> main.pdf via latexmk (preferred)
#   make figures  -> copy ../bench/*.png into figures/
#   make clean    -> remove aux files
#   make distclean -> also remove main.pdf
#
# Requires: texlive-latex-recommended + texlive-science (for algorithmicx,
# siunitx, booktabs, etc). On Ubuntu:
#   sudo apt install texlive-latex-recommended texlive-latex-extra \
#                    texlive-science latexmk

MAIN   = main
SLIDES = slides
FIGURES_SRC = ../bench
FIGURES_DST = figures

all: $(MAIN).pdf $(SLIDES).pdf

$(MAIN).pdf: $(MAIN).tex refs.bib | figures
	@if command -v latexmk >/dev/null; then \
		latexmk -pdf -interaction=nonstopmode -halt-on-error $(MAIN).tex; \
	else \
		pdflatex -interaction=nonstopmode $(MAIN).tex; \
		bibtex $(MAIN) || true; \
		pdflatex -interaction=nonstopmode $(MAIN).tex; \
		pdflatex -interaction=nonstopmode $(MAIN).tex; \
	fi

$(SLIDES).pdf: $(SLIDES).tex | figures
	@if command -v latexmk >/dev/null; then \
		latexmk -pdf -interaction=nonstopmode -halt-on-error $(SLIDES).tex; \
	else \
		pdflatex -interaction=nonstopmode $(SLIDES).tex; \
		pdflatex -interaction=nonstopmode $(SLIDES).tex; \
	fi

FIGS = speedup.png efficiency.png strong_runtime.png scaling_n.png \
       granularity_balanced.png granularity_imbalanced.png \
       weak_efficiency.png hybrid_time.png

figures:
	mkdir -p $(FIGURES_DST)
	@# Copy any benchmark PNGs that exist; don't fail if they're not built yet.
	@for f in $(FIGS); do \
		if [ -f $(FIGURES_SRC)/$$f ]; then cp $(FIGURES_SRC)/$$f $(FIGURES_DST)/; fi; \
	done
	@if [ -f ../data/sim.png ]; then cp ../data/sim.png $(FIGURES_DST)/; fi

clean:
	rm -f $(MAIN).aux $(MAIN).log $(MAIN).out $(MAIN).toc \
	      $(MAIN).bbl $(MAIN).blg $(MAIN).fls $(MAIN).fdb_latexmk \
	      $(MAIN).synctex.gz \
	      $(SLIDES).aux $(SLIDES).log $(SLIDES).out $(SLIDES).toc \
	      $(SLIDES).nav $(SLIDES).snm $(SLIDES).vrb \
	      $(SLIDES).fls $(SLIDES).fdb_latexmk $(SLIDES).synctex.gz

distclean: clean
	rm -f $(MAIN).pdf $(SLIDES).pdf

.PHONY: all figures clean distclean
