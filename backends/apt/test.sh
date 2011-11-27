#!/bin/sh
nosetests $@ --with-coverage --cover-package=aptDBUSBackend --pdb tests.py
