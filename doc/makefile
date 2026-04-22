# Quick makefile to create developer-guidelines.html

developer-guidelines.html: developer-guidelines.xml developer-guidelines.css
	xsltproc --nonet --stringparam html.stylesheet developer-guidelines.css -o developer-guidelines.html /usr/share/sgml/docbook/xsl-stylesheets/html/docbook.xsl developer-guidelines.xml

clean:
	rm -f developer-guidelines.html developer-guidelines.html.last jade-out.fot
