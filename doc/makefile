# Quick makefile to create developer-guidelines.html

developer-guidelines.html: developer-guidelines.sgml
	if which docbook2html > /dev/null 2>&1; then \
		docbook2html -u developer-guidelines.sgml ;\
		if [[ -e developer-guidelines/developer-guidelines.html ]] ; then \
			mv developer-guidelines/developer-guidelines.html . ;\
			rm -f developer-guidelines ;\
		fi ;\
	fi
