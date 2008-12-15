import os.path
import ruckyum

def add_excludes():
    yum = ruckyum.get_yum ()

    for (repostr, lock) in get_locks():
        if repostr == None:
            repostr = '*'

        for repo in yum.repos.findRepos(repostr):
            excludes = repo.getAttribute('exclude')
            if excludes is None:
                excludes = []

            excludes.append(lock)
            repo.setAttribute('exclude', excludes)

    for repo in yum.repos.listEnabled():
        try:
            yum.excludePackages(repo)
        except:
            #sometimes it bitches about pkgSack not existing, wtf?
            pass

def get_locks():
    locks = []

    try:
        f = file(os.path.join(ruckyum.get_yum().conf.cachedir, 'locks'))
        lines = f.readlines()
        for line in lines:
            split_line = line.strip().split(';')
            repo = split_line[0]
            if repo == '':
                repo = None

            locks.append((repo, split_line[1]))

        f.close()
    except IOError:
        locks = []

    return locks

def add_lock(lock, repo=None):
    locks = get_locks()
    locks.append((repo, lock))
    save_locks(locks)

def remove_lock(index):
    locks = get_locks()
    locks.remove(locks[index])
    save_locks(locks)

def save_locks(locks):
    f = file(os.path.join(ruckyum.get_yum().conf.cachedir, 'locks'), 'w+')
    for (repo, lock) in locks:
        if repo is None:
            repo = ''

        f.write("%s;%s\n" % (repo, lock))

    f.close()

def init():
    ruckyum.init_funcs.append(add_excludes)
