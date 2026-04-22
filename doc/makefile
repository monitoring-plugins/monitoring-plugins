# Quick makefile to create developer-guidelines.html

DOCBOOK_XSL := $(firstword $(wildcard \
	/usr/share/sgml/docbook/xsl-stylesheets/html/docbook.xsl \
	/usr/share/xml/docbook/stylesheet/docbook-xsl/html/docbook.xsl))

ifeq ($(DOCBOOK_XSL),)
$(error Could not find DocBook XSL stylesheet; install docbook-xsl)
endif

developer-guidelines.html: developer-guidelines.xml developer-guidelines.css
	xsltproc --nonet \
		--stringparam html.stylesheet developer-guidelines.css \
		-o developer-guidelines.html \
		$(DOCBOOK_XSL) \
		developer-guidelines.xml

clean:
	rm -f developer-guidelines.html developer-guidelines.html.last jade-out.fot
