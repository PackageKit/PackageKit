#!/usr/bin/python
import pprint as p
import logging as log
import pdb
log.basicConfig(level=log.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s\t\t\t',
     filename='/tmp/conarybackend.log',
                    filemode='a'
    )


def pprint(str):
    log.info(str)
#    log.info( p.pprint( str, width= 10) )
