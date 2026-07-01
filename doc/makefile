# Quick makefile to create developer-guidelines.html

DOCBOOK_XSL := $(firstword $(wildcard \
	/usr/share/sgml/docbook/xsl-stylesheets/html/docbook.xsl \
	/usr/share/xml/docbook/stylesheet/docbook-xsl/html/docbook.xsl))

ifeq ($(DOCBOOK_XSL),)
$(error Could not find DocBook XSL stylesheet; install docbook-xsl)
endif

developer-guidelines.html: developer-guidelines.xml developer-guidelines.css customization.xsl
	xsltproc --nonet \
		--stringparam html.stylesheet developer-guidelines.css \
		--stringparam section.autolabel 1 \
		-o developer-guidelines.html \
		customization.xsl \
		developer-guidelines.xml

clean:
	rm -f developer-guidelines.html developer-guidelines.html.last jade-out.fot
