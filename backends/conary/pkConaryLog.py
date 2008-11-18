#!/usr/bin/python

import logging as log
import pdb
log.basicConfig(level=log.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s\t\t\t',
     filename='/tmp/conarybackend.log',
                    filemode='a'
    )
