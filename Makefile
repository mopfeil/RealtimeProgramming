# make script  -> build/script.pdf
# make slides  -> build/slides.pdf
# make         -> both
all: script slides

script:
	latexmk -pdf -outdir=build -jobname=script -interaction=nonstopmode \
	  -pdflatex='pdflatex %O "\def\lecturemode{script}\input{%S}"' main.tex

slides:
	latexmk -pdf -outdir=build -jobname=slides -interaction=nonstopmode \
	  -pdflatex='pdflatex %O "\def\lecturemode{slides}\input{%S}"' main.tex

clean:
	rm -rf build
.PHONY: all script slides clean
