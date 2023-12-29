Releasing a New Monitoring Plugins Version
==========================================

Throughout this document, it is assumed that the current Monitoring
Plugins version is 2.3.4, and that we're about to publish version 2.4.
It is also assumed that the official repository on GitHub is tracked
using the remote name `monitoring-plugins` (rather than `origin`).

Before you start
----------------

- Check Github Actions status.
- Update local Git repository to the current `master` tip.  For a
  maintenance release (e.g., version 2.3.4), update to the current
  `maint-2.3` tip, instead.

Prepare and commit files
------------------------

- Update `configure.ac` and `NP-VERSION-GEN` with new version.
- Update `NEWS` from `git log --reverse v2.3.4..` output, and specify
  the release version/date.
- Update `AUTHORS` if there are new team members.
- Update `THANKS.in` using `tools/update-thanks`.
- Commit the results:

        git commit configure.ac NP-VERSION-GEN NEWS AUTHORS THANKS.in

Create annotated tag
--------------------

    git tag -a -m 'Monitoring Plugins 2.4' v2.4

Push the code and tag to GitHub
-------------------------------

    git push monitoring-plugins master
    git push monitoring-plugins v2.4

Create new maintenance branch
-----------------------------

_Only necessary when creating a feature release._

    git checkout -b maint-2.4 v2.4
    git push -u monitoring-plugins maint-2.4

Checkout new version
--------------------

    rm -rf /tmp/plugins
    git archive --prefix=tmp/plugins/ v2.4 | (cd /; tar -xf -)

Build the tarball
-----------------

    cd /tmp/plugins
    tools/setup     # requires docbook to be installed
    ./configure
    make dist

Upload tarball to web site
--------------------------

    scp monitoring-plugins-2.4.tar.gz \
        plugins@orwell.monitoring-plugins.org:web/download/

Generate SHA1 checksum file on web site
---------------------------------------

    ssh plugins@orwell.monitoring-plugins.org \
        '(cd web/download; $HOME/bin/create-checksum monitoring-plugins-2.4.tar.gz)'

Announce new release
--------------------

- In the site.git repository:

    - Create `web/input/news/release-2-4.md`.
    - Update the `plugins_release` version in `web/macros.py`.
    - Commit and push the result:

            git add web/input/news/release-2-4.md
            git commit web/input/news/release-2-4.md web/macros.py
            git push origin master

- Post an announcement on (at least) the following mailing lists:

    - <announce@monitoring-plugins.org>
    - <help@monitoring-plugins.org> (set `Reply-To:` to this one)

- Ask the social media department to announce the release on Twitter :-)

If you want to mention the number of contributors in the announcement:

    git shortlog -s v2.3.4..v2.4 | wc -l

<!-- vim:set filetype=markdown textwidth=72: -->
