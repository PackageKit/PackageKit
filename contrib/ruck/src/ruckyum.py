import sys
import os

import rucktalk
import ruckformat

import rpm
from yum.constants import *
from i18n import _

__yum = None
init_funcs = []


def get_yum(plugins=True, repos=True, cache_only=False):
    global __yum
    global init_funcs

    if __yum is None:
        import yum
        __yum = yum.YumBase()
        __yum.logger.disabled = 1
        __yum.verbose_logger.disabled = 1

        __yum.doConfigSetup(init_plugins=plugins)

        __yum.repos.setProgressBar(RuckMeter())
        __yum.repos.callback = CacheProgressCallback()
        __yum.dsCallback = DepSolveProgressCallback()

        if cache_only or not repos:
            __yum.conf.cache = 1

        __yum.doTsSetup()
        __yum.doRpmDBSetup()

        if repos:
            __yum.doRepoSetup()
            __yum.doSackSetup()

        for func in init_funcs:
                func()

    return __yum




from urlgrabber.progress import BaseMeter
class RuckMeter(BaseMeter):

    def __init__(self, fo=sys.stdout):
        BaseMeter.__init__(self)
        self.fo = fo
        self.last_text = None

    def _do_start(self, now=None):
        if self.text is not None:
            text = self.text
        else:
            text = self.basename

        rucktalk.message(text)

    def _do_update(self, amount_read, now=None):
        rucktalk.message_status(ruckformat.progress_to_str(self.re.fraction_read() * 100,
                                                         self.last_amount_read, self.size, self.re.remaining_time(),
                                                         self.re.elapsed_time()))




    def _do_end(self, amount_read, now=None):
        rucktalk.message_status(ruckformat.progress_to_str(self.re.fraction_read() * 100,
                                                         self.last_amount_read, self.size, self.re.remaining_time(),
                                                         self.re.elapsed_time()))

        self.fo.write('\n\n')
        self.fo.flush()

class CacheProgressCallback:

    '''
    The class handles text output callbacks during metadata cache updates.
    '''

    def __init__(self):
        self.last_text = None

    def log(self, level, message):
        pass

    def errorlog(self, level, message):
        rucktalk.error(message)

    def filelog(self, level, message):
        pass

    def progressbar(self, current, total, name=None):
        if current > total:
            return

        msg = 'Updating metadata'
        if name != None:
            msg = "Updating repository: %s" % name

        if msg != self.last_text:
            rucktalk.message_finished(msg)
            self.last_text = msg

        rucktalk.message_status(ruckformat.progress_to_str(float(current) / float(total) * 100,
                                                         -1, -1, -1, -1))
        if current == total:
            rucktalk.message('\n')
            self.last_text = None

class RPMInstallCallback:
    def __init__(self, output=1):
        self.output = output
        self.callbackfilehandles = {}
        self.total_actions = 0
        self.total_installed = 0
        self.installed_pkg_names = []
        self.total_removed = 0
        self.last_message = None

        self.myprocess = { TS_UPDATE : 'Updating',
                           TS_ERASE: 'Erasing',
                           TS_INSTALL: 'Installing',
                           TS_TRUEINSTALL : 'Installing',
                           TS_OBSOLETED: 'Obsoleted',
                           TS_OBSOLETING: 'Installing'}
        self.mypostprocess = { TS_UPDATE: 'Updated',
                               TS_ERASE: 'Erased',
                               TS_INSTALL: 'Installed',
                               TS_TRUEINSTALL: 'Installed',
                               TS_OBSOLETED: 'Obsoleted',
                               TS_OBSOLETING: 'Installed'}

        self.tsInfo = None # this needs to be set for anything else to work

    def _dopkgtup(self, hdr):
        tmpepoch = hdr['epoch']
        if tmpepoch is None: epoch = '0'
        else: epoch = str(tmpepoch)

        return (hdr['name'], hdr['arch'], epoch, hdr['version'], hdr['release'])

    def _makeHandle(self, hdr):
        handle = '%s:%s.%s-%s-%s' % (hdr['epoch'], hdr['name'], hdr['version'],
          hdr['release'], hdr['arch'])

        return handle

    def _localprint(self, msg):
        if self.output:
            rucktalk.message(msg)

    def show_progress(self, percent, process, name):
        msg = "(%s/%s) %s: %s" % (self.total_installed + self.total_removed, self.total_actions, process, name)
        rucktalk.message_status(ruckformat.progress_to_str(percent, -1, -1, -1, -1, text=msg))

    def callback(self, what, bytes, total, h, user):
        if what == rpm.RPMCALLBACK_TRANS_START:
            if bytes == 6:
                self.total_actions = total

        elif what == rpm.RPMCALLBACK_TRANS_PROGRESS:
            pass

        elif what == rpm.RPMCALLBACK_TRANS_STOP:
            pass

        elif what == rpm.RPMCALLBACK_INST_OPEN_FILE:

            hdr = None
            if h is not None:
                hdr, rpmloc = h
                handle = self._makeHandle(hdr)
                fd = os.open(rpmloc, os.O_RDONLY)
                self.callbackfilehandles[handle]=fd
                self.total_installed += 1
                self.installed_pkg_names.append(hdr['name'])
                return fd
            else:
                self._localprint("No header - huh?")

        elif what == rpm.RPMCALLBACK_INST_CLOSE_FILE:
            hdr = None
            if h is not None:
                hdr, rpmloc = h
                handle = self._makeHandle(hdr)
                os.close(self.callbackfilehandles[handle])
                fd = 0

                if self.output:
                    rucktalk.message('')

        elif what == rpm.RPMCALLBACK_INST_PROGRESS:
            if h is not None:
                # If h is a string, we're repackaging.
                # Why the RPMCALLBACK_REPACKAGE_PROGRESS flag isn't set, I have no idea
                if type(h) == type(""):
                    if total == 0:
                        percent = 0
                    else:
                        percent = (bytes*100L)/total
                    if self.output and sys.stdout.isatty():
                        self.show_progress(percent, 'Repackage', h)

                        if bytes == total:
                            sys.stdout.write('\n')
                            sys.stdout.flush()
                else:
                    hdr, rpmloc = h
                    if total == 0:
                        percent = 0
                    else:
                        percent = (bytes*100L)/total
                    pkgtup = self._dopkgtup(hdr)

                    txmbrs = self.tsInfo.getMembers(pkgtup=pkgtup)
                    for txmbr in txmbrs:
                        try:
                            process = self.myprocess[txmbr.output_state]
                        except KeyError, e:
                            rucktalk.message("Error: invalid output state: %s for %s" % \
                                            (txmbr.output_state, hdr['name']))
                        else:
                            if self.output and (sys.stdout.isatty() or bytes == total):
                                self.show_progress(percent, process, hdr['name'])


        elif what == rpm.RPMCALLBACK_UNINST_START:
            pass

        elif what == rpm.RPMCALLBACK_UNINST_PROGRESS:
            pass

        elif what == rpm.RPMCALLBACK_UNINST_STOP:
            self.total_removed += 1

            if self.output and sys.stdout.isatty():
                if h not in self.installed_pkg_names:
                    process = "Removing"
                else:
                    process = "Cleanup"
                percent = 100

                self.show_progress(percent, process, h)
                rucktalk.message('')

        elif what == rpm.RPMCALLBACK_REPACKAGE_START:
            pass
        elif what == rpm.RPMCALLBACK_REPACKAGE_STOP:
            pass
        elif what == rpm.RPMCALLBACK_REPACKAGE_PROGRESS:
            pass


class DepSolveProgressCallback:
    """provides text output callback functions for Dependency Solver callback"""

    def __init__(self):
        self.loops = 0

    def pkgAdded(self, pkgtup, mode):
        pass

    def start(self):
        self.loops += 1

    def tscheck(self):
        pass

    def restartLoop(self):
        pass

    def end(self):
        pass

    def procReq(self, name, formatted_req):
        pass

    def unresolved(self, msg):
        pass

    def procConflict(self, name, confname):
        pass

    def transactionPopulation(self):
        pass

    def downloadHeader(self, name):
        rucktalk.message("Downloading header for '%s'" % name)
