#!/bin/sh
spec_to_docbook_xsl="$1"
input_file="$2"
output_file="$3"

echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > ${output_file}
xsltproc ${spec_to_docbook_xsl} ${input_file} | tail -n +2 >> ${output_file}
