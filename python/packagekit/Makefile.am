BUILT_SOURCES = enums.py

enums.py: ../enum-convertor.py ../../libpackagekit/pk-enum.c
	(cd ..; python enum-convertor.py > packagekit/enums.py)
