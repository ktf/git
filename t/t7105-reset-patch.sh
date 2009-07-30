#!/bin/sh

test_description='git reset --patch'
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir dir &&
	echo parent > dir/foo &&
	echo dummy > dir/bar &&
	git add dir &&
	git commit -m initial &&
	test_tick &&
	test_commit second dir/foo head &&
	echo work > dir/foo &&
	echo bar_index > dir/bar &&
	git add dir/bar &&
	echo bar_work > dir/bar
'

# note: bar sorts before foo, so the first 'n' is always to skip 'bar'

test_expect_success 'saying "n" does nothing' '
	git add dir/foo &&
	(echo n; echo n) | git reset -p &&
	test "$(git show :dir/foo)" = work &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git reset -p' '
	git add dir/foo &&
	(echo n; echo y) | git reset -p &&
	test "$(git show :dir/foo)" = head &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_expect_success 'git reset -p HEAD^' '
	(echo n; echo y) | git reset -p HEAD^ &&
	test "$(git show :dir/foo)" = parent &&
	test "$(cat dir/foo)" = work &&
	test "$(git show :dir/bar)" = bar_index &&
	test "$(cat dir/bar)" = bar_work
'

test_done
