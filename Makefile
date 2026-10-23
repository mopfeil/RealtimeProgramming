# make script      -> build/script.pdf
# make slides      -> build/slides.pdf
# make pptx        -> pdf/RealtimeProgramming_Slides.pptx (slides as pictures,
#                     script text as speaker notes, see pptx/build_pptx.py)
# make check-pptx  -> fails if the deck is older than the slide sources
# make hooks       -> git pre-commit hook that runs check-pptx
# make             -> all of them
all: script slides pptx

script:
	latexmk -pdf -outdir=build -jobname=script -interaction=nonstopmode \
	  -pdflatex='pdflatex %O "\def\lecturemode{script}\input{%S}"' main.tex

slides:
	latexmk -pdf -outdir=build -jobname=slides -interaction=nonstopmode \
	  -pdflatex='pdflatex %O "\def\lecturemode{slides}\input{%S}"' main.tex

# Python packages for pptx go into a local venv on the first run
PPTX_PY = pptx/.venv/bin/python

pptx/.venv/installed: pptx/requirements.txt
	python3 -m venv pptx/.venv
	$(PPTX_PY) -m pip install -q -r pptx/requirements.txt
	touch $@

pptx: script slides pptx/.venv/installed
	$(PPTX_PY) pptx/build_pptx.py build/slides.pdf pdf/RealtimeProgramming_Slides.pptx

check-pptx:
	python3 pptx/build_pptx.py --check

hooks:
	cp pptx/pre-commit .git/hooks/pre-commit

clean:
	rm -rf build
.PHONY: all script slides pptx check-pptx hooks clean
