#!/bin/sh

test_description='git checkout --patch'
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir dir &&
	echo parent > dir/foo &&
	echo dummy > dir/bar &&
	git add dir &&
	git commit -m initial &&
	test_tick &&
	test_commit second dir/foo head &&
	echo index > dir/foo &&
	git add dir/foo &&
	echo work > dir/foo &&
	echo bar_index > dir/bar &&
	git add dir/bar &&
	echo bar_work > dir/bar
'

# note: bar sorts before foo, so the first 'n' is always to skip 'bar'

test_expect_success 'saying "n" does nothing' '
	(echo n; echo n) | test_must_fail git stash save -p &&
	test "$(git show :dir/foo)" = index &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git stash -p' '
	(echo n; echo y) | git stash save -p &&
	test "$(git show :dir/foo)" = index &&
	test "$(cat dir/foo)" = head &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work &&
	git reset --hard &&
	git stash apply &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = dummy &&
	test "$(cat dir/bar)" = dummy
'

test_expect_success 'git stash -p --no-keep-index' '
	echo index > dir/foo &&
	git add dir/foo &&
	echo work > dir/foo &&
	echo bar_index > dir/bar &&
	git add dir/bar &&
	echo bar_work > dir/bar &&
	(echo n; echo y) | git stash save -p --no-keep-index &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = head &&
	test "$(git show :dir/bar)" = dummy &&
	test "$(cat dir/bar)" = bar_work &&
	git reset --hard &&
	git stash apply --index &&
	test "$(git show :dir/foo)" = index &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = dummy
'

test_done
