#!/bin/sh
#
# Copyright (c) 2009 Robert Allan Zeh

test_description='git svn gc basic tests'

. ./lib-git-svn.sh

test_expect_success 'setup directories and test repo' '
	mkdir import &&
	mkdir tmp &&
	echo "Sample text for Subversion repository." > import/test.txt &&
	svn_cmd import -m "import for git svn" import "$svnrepo" > /dev/null
	'

test_expect_success 'checkout working copy from svn' \
	'svn_cmd co "$svnrepo" test_wc'

test_expect_success 'set some properties to create an unhandled.log file' '
	(
		cd test_wc &&
		svn_cmd propset foo bar test.txt &&
		svn_cmd commit -m "property set"
	)'

test_expect_success 'Setup repo' 'git svn init "$svnrepo"'

test_expect_success 'Fetch repo' 'git svn fetch'

test_expect_success 'make backup copy of unhandled.log' '
	 cp .git/svn/git-svn/unhandled.log tmp
	'

test_expect_success 'create leftover index' '> .git/svn/git-svn/index'

test_expect_success 'git svn gc runs' 'git svn gc'

test_expect_success 'git svn index removed' '! test -f .git/svn/git-svn/index'

if perl -MCompress::Zlib -e 0 2>/dev/null
then
	test_expect_success 'git svn gc produces a valid gzip file' '
		 gunzip .git/svn/git-svn/unhandled.log.gz
		'
else
	say "Perl Compress::Zlib unavailable, skipping gunzip test"
fi

test_expect_success 'git svn gc does not change unhandled.log files' '
	 test_cmp .git/svn/git-svn/unhandled.log tmp/unhandled.log
	'

test_done
