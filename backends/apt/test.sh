#!/bin/sh
nosetests $@ --with-coverage --cover-package=aptBackend --pdb tests
