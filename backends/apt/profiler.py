#!/usr/bin/env python

import hotshot
import sys

import aptBackend

def main():
    prof = hotshot.Profile("%s.prof" % sys.argv[1])
    backend = aptBackend.PackageKitAptBackend(sys.argv[1:])
    prof.runcall(backend.run_command)
    prof.close()

if __name__ == '__main__':
    main()

# vim: ts=4 et sts=4
