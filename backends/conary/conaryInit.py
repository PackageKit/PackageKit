#!/usr/bin/python
from conary import conarycfg, conaryclient

def init_conary_config():
    cfg = conarycfg.ConaryConfiguration(True)
    cfg.initializeFlavors()
    cfg.autoResolve = True
    cfg.keepRequired = True
    return cfg

def init_conary_client():
    cfg = init_conary_config()
    return conaryclient.ConaryClient(cfg)

def conary_db():
    client = init_conary_client()
    return client.db
