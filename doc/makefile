# Quick makefile to create developer-guidelines.html

developer-guidelines.html: developer-guidelines.sgml
	docbook2html -u developer-guidelines.sgml
	if [ -e developer-guidelines/developer-guidelines.html ] ; then \
		mv developer-guidelines/developer-guidelines.html . ;\
		rm -rf developer-guidelines ;\
	fi

clean:
	rm -f developer-guidelines.html developer-guidelines.html.last jade-out.fot
